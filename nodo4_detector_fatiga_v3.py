import cv2
import urllib.request
import numpy as np
import time
from twilio.rest import Client
import threading
from datetime import datetime

# =============================================
# CONFIGURACIÓN DE TWILIO PARA ENVÍO DE SMS
# =============================================
account_sid = "ABCDEFGHIJK12345610"   # ID de cuenta de Twilio (reemplazar con ID real)
auth_token = "ABCDEFGHIJK12345678"      # Token de autenticación de Twilio (reemplazar con token real)
client = Client(account_sid, auth_token)

# Números de teléfono para el sistema de alertas
twilio_number = "+YYYYYYYYYY"          # Número de Twilio desde el que se envían mensajes (No se muestra por seguridad)
my_number = "+XXXXXXXXXX"              # Número al que se enviarán las alertas (No se muestra por seguridad)

# =============================================
# CONFIGURACIÓN DEL SISTEMA DE DETECCIÓN
# =============================================
# URL de la cámara del ESP32 
CAMERA_URL = 'http://192.168.198.86/capture'

# Parámetros de detección de fatiga
FATIGUE_TIME_THRESHOLD = 3.0           # Segundos sin ojos detectados para considerar fatiga
ALERT_COOLDOWN = 10.0                  # Segundos entre alertas para evitar spam
FRAME_SKIP = 2                         # Procesar 1 de cada N frames para optimizar rendimiento

# Parámetros de los clasificadores Haar
FACE_SCALE_FACTOR = 1.1                # Factor de escala para detección de caras
FACE_MIN_NEIGHBORS = 5                 # Mínimo número de vecinos para validar cara
EYE_SCALE_FACTOR = 1.05                # Factor de escala para detección de ojos
EYE_MIN_NEIGHBORS = 3                  # Mínimo número de vecinos para validar ojos

# =============================================
# CARGAR CLASIFICADORES HAAR PARA DETECCIÓN
# =============================================
try:
    # Cargar clasificador para detectar caras frontales
    face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')
    # Cargar clasificador para detectar ojos
    eye_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_eye.xml')
    
    # Verificar que los clasificadores se cargaron correctamente
    if face_cascade.empty() or eye_cascade.empty():
        raise Exception("No se pudieron cargar los clasificadores Haar")
    
    print("Clasificadores Haar cargados correctamente")
except Exception as e:
    print(f"Error cargando clasificadores: {e}")
    exit(1)

# =============================================
# VARIABLES GLOBALES DEL SISTEMA
# =============================================
# Control de tiempo y estado
eyes_closed_start_time = None          # Momento cuando se dejaron de detectar ojos
last_alert_time = 0                    # Último momento que se envió una alerta
frame_count = 0                        # Contador de frames para optimización
is_fatigue_detected = False            # Flag para indicar si hay fatiga detectada
last_sms_sent = False                  # Flag para controlar envío de SMS

# Estadísticas del sistema
total_alerts = 0                       # Contador total de alertas enviadas
session_start_time = time.time()       # Inicio de la sesión actual

# =============================================
# FUNCIÓN PARA ENVIAR ALERTAS SMS 
# =============================================
def send_alert_sms():
    """
    Envía un SMS de alerta de fatiga usando Twilio en un hilo separado
    para no bloquear el procesamiento de video
    """
    global total_alerts
    try:
        current_time = datetime.now().strftime("%H:%M:%S")
        message_body = f"ALERTA DE FATIGA DETECTADA \nHora: {current_time}\nManténgase alerta al conducir."
        
        # Enviar mensaje usando la API de Twilio
        message = client.messages.create(
            body=message_body,
            from_=twilio_number,
            to=my_number
        )
        
        total_alerts += 1
        print(f"SMS enviado exitosamente. SID: {message.sid}")
        print(f"Total de alertas enviadas: {total_alerts}")
        
    except Exception as e:
        print(f"Error enviando SMS: {e}")

# =============================================
# FUNCIÓN PARA OBTENER FRAME DE LA CÁMARA ESP32
# =============================================
def get_camera_frame():
    """
    Obtiene un frame de la cámara del ESP32 con manejo de errores
    """
    try:
        # Realizar petición HTTP a la cámara del ESP32
        img_resp = urllib.request.urlopen(CAMERA_URL, timeout=5)
        img_arr = np.array(bytearray(img_resp.read()), dtype=np.uint8)
        img = cv2.imdecode(img_arr, cv2.IMREAD_COLOR)
        
        # Verificar que la imagen se decodificó correctamente
        if img is None:
            raise Exception("No se pudo decodificar la imagen")
            
        return img
    
    except Exception as e:
        print(f"Error obteniendo imagen de cámara: {e}")
        return None

# =============================================
# FUNCIÓN PRINCIPAL PARA DETECTAR FATIGA
# =============================================
def detect_fatigue(img):
    """
    Detecta signos de fatiga analizando si los ojos están cerrados
    Retorna True si se detecta fatiga, False en caso contrario
    """
    global eyes_closed_start_time, is_fatigue_detected
    
    # Convertir imagen a escala de grises para mejor procesamiento
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    
    # Detectar caras en la imagen
    faces = face_cascade.detectMultiScale(
        gray, 
        scaleFactor=FACE_SCALE_FACTOR, 
        minNeighbors=FACE_MIN_NEIGHBORS,
        minSize=(30, 30)  # Tamaño mínimo de cara para evitar falsos positivos
    )
    
    eyes_detected = False
    
    # Procesar cada cara detectada
    for (x, y, w, h) in faces:
        # Dibujar rectángulo alrededor de la cara detectada
        cv2.rectangle(img, (x, y), (x+w, y+h), (255, 0, 0), 2)
        
        # Definir región de interés (ROI) para buscar ojos solo en la cara
        roi_gray = gray[y:y+h, x:x+w]
        roi_color = img[y:y+h, x:x+w]
        
        # Detectar ojos dentro de la región de la cara
        eyes = eye_cascade.detectMultiScale(
            roi_gray,
            scaleFactor=EYE_SCALE_FACTOR,
            minNeighbors=EYE_MIN_NEIGHBORS,
            minSize=(10, 10)  # Tamaño mínimo de ojo
        )
        
        # Dibujar círculos alrededor de cada ojo detectado
        for (ex, ey, ew, eh) in eyes:
            center_x = ex + ew // 2
            center_y = ey + eh // 2
            radius = max(ew, eh) // 2
            cv2.circle(roi_color, (center_x, center_y), radius, (0, 255, 0), 2)
            eyes_detected = True
        
        # Mostrar información de estado en la imagen
        status_text = "OJOS ABIERTOS" if eyes_detected else "OJOS CERRADOS"
        status_color = (0, 255, 0) if eyes_detected else (0, 0, 255)
        cv2.putText(img, status_text, (x, y-10), cv2.FONT_HERSHEY_SIMPLEX, 0.7, status_color, 2)
    
    # Lógica de detección de fatiga basada en tiempo sin ojos detectados
    current_time = time.time()
    
    if eyes_detected:
        # Si se detectan ojos, reiniciar el temporizador de fatiga
        eyes_closed_start_time = None
        is_fatigue_detected = False
        return False
    else:
        # Si no se detectan ojos, iniciar o continuar conteo
        if eyes_closed_start_time is None:
            eyes_closed_start_time = current_time
        
        # Calcular tiempo transcurrido sin detectar ojos
        time_without_eyes = current_time - eyes_closed_start_time
        
        # Mostrar temporizador en pantalla
        cv2.putText(img, f"Sin ojos: {time_without_eyes:.1f}s", 
                   (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 165, 255), 2)
        
        # Detectar fatiga si ha pasado el tiempo umbral
        if time_without_eyes >= FATIGUE_TIME_THRESHOLD:
            is_fatigue_detected = True
            return True
    
    return False

# =============================================
# FUNCIÓN PARA MOSTRAR INFORMACIÓN EN PANTALLA
# =============================================
def draw_system_info(img):
    """
    Dibuja información del sistema en la imagen
    """
    # Información de sesión
    session_time = int(time.time() - session_start_time)
    cv2.putText(img, f"Tiempo sesion: {session_time}s", 
               (10, img.shape[0] - 70), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
    
    # Total de alertas
    cv2.putText(img, f"Alertas enviadas: {total_alerts}", 
               (10, img.shape[0] - 50), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
    
    # Estado del sistema
    if is_fatigue_detected:
        cv2.putText(img, "FATIGA DETECTADA!", 
                   (10, 70), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 255), 3)
    
    # Instrucciones
    cv2.putText(img, "Presiona 'q' para salir", 
               (10, img.shape[0] - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (200, 200, 200), 1)

# =============================================
# BUCLE PRINCIPAL DEL PROGRAMA
# =============================================
def main():
    """
    Función principal que ejecuta el detector de fatiga
    """
    global frame_count, last_alert_time, last_sms_sent
    
    # Crear ventana para mostrar el video
    cv2.namedWindow('Detector de Fatiga del Conductor', cv2.WINDOW_AUTOSIZE)
    
    print("Iniciando detector de fatiga del conductor...")
    print("Conectando con cámara ESP32...")
    print("Sistema de alertas SMS configurado")
    print("ADVERTENCIA: Mantenga los ojos abiertos mientras conduce")
    print("-" * 60)
    
    try:
        while True:
            frame_count += 1
            
            # Optimización: procesar solo algunos frames
            if frame_count % FRAME_SKIP != 0:
                continue
            
            # Obtener frame de la cámara
            img = get_camera_frame()
            if img is None:
                continue
            
            # Detectar fatiga en el frame actual
            fatigue_detected = detect_fatigue(img)
            
            # Manejar alerta de fatiga
            current_time = time.time()
            if fatigue_detected and not last_sms_sent:
                # Verificar que ha pasado suficiente tiempo desde la última alerta
                if current_time - last_alert_time >= ALERT_COOLDOWN:
                    # Enviar SMS en hilo separado para no bloquear video
                    sms_thread = threading.Thread(target=send_alert_sms)
                    sms_thread.daemon = True
                    sms_thread.start()
                    
                    last_alert_time = current_time
                    last_sms_sent = True
                    print(f"FATIGA DETECTADA - Enviando alerta SMS...")
            
            # Reset del flag de SMS cuando se detectan ojos nuevamente
            if not fatigue_detected and last_sms_sent:
                last_sms_sent = False
                print("Ojos detectados nuevamente - Sistema normalizado")
            
            # Dibujar información del sistema en la imagen
            draw_system_info(img)
            
            # Mostrar la imagen procesada
            cv2.imshow('Detector de Fatiga del Conductor', img)
            
            # Salir si se presiona 'q'
            if cv2.waitKey(1) & 0xFF == ord('q'):
                print("\nCerrando detector de fatiga...")
                break
                
    except KeyboardInterrupt:
        print("\nPrograma interrumpido por el usuario")
    except Exception as e:
        print(f"Error inesperado: {e}")
    finally:
        # Limpiar recursos
        cv2.destroyAllWindows()
        print("Recursos liberados correctamente")
        print(f"Estadísticas de la sesión:")
        print(f"   - Tiempo total: {int(time.time() - session_start_time)} segundos")
        print(f"   - Alertas enviadas: {total_alerts}")

# =============================================
# PUNTO DE ENTRADA DEL PROGRAMA
# =============================================
if __name__ == "__main__":
    main()