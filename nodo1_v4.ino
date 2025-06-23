#include <Arduino.h>

// =============================================
// DEFINICIÓN DE PINES DEL SISTEMA
// =============================================
const int PIN_TRIGGER = 6;               // Pin para activar el sensor ultrasónico HC-SR04
const int PIN_ECHO = 5;                  // Pin que recibe el eco del sensor ultrasónico
const int PIN_HABILITAR = 7;             // Pin que habilita el sensor de evitación
const int PIN_SENSOR_EVITACION = 9;      // Pin del sensor de evitación de obstáculos
const int PIN_PIR = 8;                   // Pin del sensor PIR que detecta movimiento
const int PIN_ZUMBADOR = 11;             // Pin para el zumbador/alarma sonora
const int PIN_LED_ROJO = 12;             // Pin del LED rojo (indica peligro/alerta)
const int PIN_LED_VERDE = 13;            // Pin del LED verde (indica movimiento)
const int PIN_LED_AZUL = 10;             // Pin del LED azul (indica objeto cercano)

// =============================================
// CONSTANTES DEL SISTEMA
// =============================================
const int UMBRAL_DISTANCIA = 15;         // Distancia mínima en centímetros para activar alarma
const int TIMEOUT_ULTRASONICO = 30000;   // Timeout en microsegundos para sensor ultrasónico (~5m máximo)
const int RETRASO_BUCLE = 100;           // Pausa en milisegundos entre cada ciclo del programa
const int DURACION_TONO = 200;           // Duración del tono del buzzer en milisegundos

// =============================================
// VARIABLES GLOBALES
// =============================================
unsigned long ultimaMedicion = 0;                    // Guarda el tiempo de la última medición ultrasónica
const unsigned long INTERVALO_MEDICION = 50;         // Intervalo mínimo entre mediciones en milisegundos

// =============================================
// ESTRUCTURA PARA ORGANIZAR EL ESTADO DEL SISTEMA
// =============================================
struct EstadoSistema {
  bool objetoCercano;        // True si hay un objeto a menos de 15cm
  bool sensorEvitacion;      // True si el sensor de evitación detecta algo
  bool movimientoDetectado;  // True si el PIR detecta movimiento
  int distancia;             // Distancia medida por el sensor ultrasónico en cm
};

// =============================================
// CONFIGURACIÓN INICIAL DEL SISTEMA
// =============================================
void setup() {
  Serial.begin(9600);                        // Inicia la comunicación serie a 9600 baudios para debug
  
  // Configuración de todos los pines del sistema
  pinMode(PIN_TRIGGER, OUTPUT);              // Configura el Trigger del ultrasónico como salida
  pinMode(PIN_ECHO, INPUT);                  // Configura el Echo del ultrasónico como entrada
  pinMode(PIN_SENSOR_EVITACION, INPUT);      // El sensor de evitación es una entrada digital
  pinMode(PIN_PIR, INPUT);                   // El sensor PIR es también una entrada digital
  pinMode(PIN_ZUMBADOR, OUTPUT);             // Configura el zumbador como salida
  pinMode(PIN_LED_ROJO, OUTPUT);             // Configura el LED rojo como salida
  pinMode(PIN_LED_VERDE, OUTPUT);            // Configura el LED verde como salida
  pinMode(PIN_LED_AZUL, OUTPUT);             // Configura el LED azul como salida
  pinMode(PIN_HABILITAR, OUTPUT);            // Pin para habilitar el sensor de evitación
  
  // Establecer estado inicial seguro de todos los componentes
  digitalWrite(PIN_TRIGGER, LOW);            // Asegura que el trigger esté apagado inicialmente
  digitalWrite(PIN_HABILITAR, HIGH);         // Habilita el sensor de evitación desde el inicio
  apagarTodosLosLeds();                      // Apaga todos los LEDs al inicio
  
  Serial.println("Sistema de alarma iniciado");
  delay(2000);                               // Tiempo de estabilización para que los sensores se inicialicen
}

// =============================================
// BUCLE PRINCIPAL DEL PROGRAMA
// =============================================
void loop() {
  EstadoSistema estado = leerSensores();     // Lee todos los sensores y guarda su estado
  
  // Función de debug opcional: envía 'd' por serial para ver información detallada
  if (Serial.available() && Serial.read() == 'd') {
    mostrarInfoDebug(estado);                // Muestra información detallada de todos los sensores
  }
  
  procesarLogicaAlarma(estado);              // Procesa la lógica de alarma según el estado de los sensores
  delay(RETRASO_BUCLE);                      // Pausa antes de la próxima lectura para evitar saturar el sistema
}

// =============================================
// FUNCIÓN PARA LEER TODOS LOS SENSORES
// =============================================
EstadoSistema leerSensores() {
  EstadoSistema estado;                      // Crea una estructura para guardar el estado de todos los sensores
  
  // Lectura del sensor ultrasónico con protección contra errores
  estado.distancia = leerDistanciaUltrasonico(); // Mide la distancia con el sensor ultrasónico
  estado.objetoCercano = (estado.distancia > 0 && estado.distancia < UMBRAL_DISTANCIA); // Determina si hay objeto cercano
  
  // Lectura del sensor de evitación (activo en LOW)
  estado.sensorEvitacion = (digitalRead(PIN_SENSOR_EVITACION) == LOW); // Lee sensor de evitación
  
  // Lectura del sensor PIR de movimiento (activo en HIGH)
  estado.movimientoDetectado = (digitalRead(PIN_PIR) == HIGH); // Lee el sensor de movimiento PIR
  
  return estado;                             // Devuelve el estado completo de todos los sensores
}

// =============================================
// FUNCIÓN PARA MEDIR DISTANCIA CON SENSOR ULTRASÓNICO
// =============================================
int leerDistanciaUltrasonico() {
  // Evita mediciones muy frecuentes que pueden causar interferencias
  if (millis() - ultimaMedicion < INTERVALO_MEDICION) {
    return -1;                               // Devuelve -1 para indicar que no se hizo una nueva medición
  }
  
  // Secuencia estándar para activar el sensor ultrasónico HC-SR04
  digitalWrite(PIN_TRIGGER, LOW);            // Asegura que el trigger esté en LOW
  delayMicroseconds(2);                      // Espera corta para limpiar la señal
  digitalWrite(PIN_TRIGGER, HIGH);           // Activa el trigger por 10 microsegundos
  delayMicroseconds(10);                     // Mantiene el trigger activo el tiempo necesario
  digitalWrite(PIN_TRIGGER, LOW);            // Desactiva el trigger para esperar el eco
  
  // Mide el tiempo que tarda en regresar el eco con timeout de seguridad
  long duracion = pulseIn(PIN_ECHO, HIGH, TIMEOUT_ULTRASONICO);
  ultimaMedicion = millis();                 // Guarda el tiempo de esta medición
  
  // Si no hay respuesta del sensor (timeout), devuelve error
  if (duracion == 0) {
    return -1;                               // Error en la medición - no hay eco
  }
  
  // Conversión de tiempo a distancia más precisa
  // Velocidad del sonido = 343 m/s a 20°C, dividido por 2 (ida y vuelta)
  int distancia = duracion * 0.017;         // Conversión: (duration/2) / 29.1 ≈ duration * 0.017
  
  // Filtro de valores válidos para el sensor HC-SR04 (rango típico: 2cm - 400cm)
  if (distancia < 2 || distancia > 400) {
    return -1;                               // Valor fuera del rango válido del sensor
  }
  
  return distancia;                          // Devuelve la distancia medida en centímetros
}

// =============================================
// LÓGICA PRINCIPAL DE PROCESAMIENTO DE ALARMAS
// =============================================
void procesarLogicaAlarma(EstadoSistema estado) {
  // Evalúa las condiciones en orden de prioridad (de mayor a menor peligro)
  
  if (estado.objetoCercano && estado.sensorEvitacion && estado.movimientoDetectado) {
    // ALERTA MÁXIMA: todos los sensores detectan peligro simultáneamente
    // Objeto muy cercano + sensor de evitación + movimiento detectado
    establecerEstadoAlarma(277, true, true, false, "ALERTA MAXIMA");
    
  } else if (estado.objetoCercano && estado.sensorEvitacion) {
    // ALERTA ALTA: objeto cercano y sensor de evitación activos
    // Indica posible colisión inminente
    establecerEstadoAlarma(350, true, false, true, "OBJETO DETECTADO + EVITACION");
    
  } else if (estado.objetoCercano && estado.movimientoDetectado) {
    // ALERTA ALTA: objeto cercano en movimiento
    // Objeto que se acerca y hay movimiento general
    establecerEstadoAlarma(320, true, true, false, "OBJETO CERCANO + MOVIMIENTO");
    
  } else if (estado.sensorEvitacion && estado.movimientoDetectado) {
    // ALERTA MEDIA: evitación y movimiento sin objeto ultracercano
    // Actividad detectada pero no crítica
    establecerEstadoAlarma(290, false, true, true, "EVITACION + MOVIMIENTO");
    
  } else if (estado.objetoCercano) {
    // ALERTA BÁSICA: solo objeto cercano detectado
    // Algo está cerca pero podría ser estático
    establecerEstadoAlarma(400, false, false, true, "OBJETO CERCANO");
    
  } else if (estado.sensorEvitacion) {
    // ALERTA BÁSICA: solo sensor de evitación activo
    // Obstáculo detectado por sensor infrarrojo
    establecerEstadoAlarma(315, true, false, false, "SENSOR EVITACION");
    
  } else if (estado.movimientoDetectado) {
    // ALERTA BÁSICA: solo movimiento detectado
    // PIR detecta movimiento pero sin objetos cercanos
    establecerEstadoAlarma(477, false, true, false, "MOVIMIENTO DETECTADO");
    
  } else {
    // ESTADO NORMAL: ningún sensor detecta peligro
    // Sistema funcionando sin alertas
    establecerEstadoAlarma(0, false, false, false, "SISTEMA OK");
  }
}

// =============================================
// FUNCIÓN PARA CONTROLAR ALARMA Y LEDs SEGÚN EL ESTADO
// =============================================
void establecerEstadoAlarma(int frecuenciaTono, bool ledRojo, bool ledVerde, bool ledAzul, const char* mensaje) {
  // Control del buzzer/zumbador
  if (frecuenciaTono > 0) {
    tone(PIN_ZUMBADOR, frecuenciaTono, DURACION_TONO);  // Activa el buzzer con la frecuencia especificada
  } else {
    noTone(PIN_ZUMBADOR);                               // Apaga el buzzer si no hay frecuencia
  }
  
  // Control individual de cada LED según el estado de alarma
  digitalWrite(PIN_LED_ROJO, ledRojo);                  // Enciende/apaga LED rojo (indica peligro/alerta)
  digitalWrite(PIN_LED_VERDE, ledVerde);                // Enciende/apaga LED verde (indica movimiento)
  digitalWrite(PIN_LED_AZUL, ledAzul);                  // Enciende/apaga LED azul (indica objeto cercano)
  
  // Registro del estado actual en el monitor serie (evita spam de mensajes iguales)
  static String ultimoMensaje = "";                     // Variable estática para recordar el último mensaje
  if (String(mensaje) != ultimoMensaje) {               // Solo imprime si el mensaje cambió
    Serial.println(mensaje);                            // Imprime el estado actual del sistema
    ultimoMensaje = String(mensaje);                    // Guarda el mensaje actual para la próxima comparación
  }
}

// =============================================
// FUNCIÓN AUXILIAR PARA APAGAR TODOS LOS LEDs
// =============================================
void apagarTodosLosLeds() {
  digitalWrite(PIN_LED_ROJO, LOW);                      // Apaga el LED rojo
  digitalWrite(PIN_LED_VERDE, LOW);                     // Apaga el LED verde
  digitalWrite(PIN_LED_AZUL, LOW);                      // Apaga el LED azul
}

// =============================================
// FUNCIÓN DE DEBUG PARA MOSTRAR INFORMACIÓN DETALLADA
// =============================================
void mostrarInfoDebug(EstadoSistema estado) {
  Serial.println("=== INFORMACION DEBUG ===");
  Serial.print("Distancia: ");
  Serial.print(estado.distancia);
  Serial.println(" cm");                                // Muestra la distancia medida por el ultrasónico
  Serial.print("Objeto cercano: ");
  Serial.println(estado.objetoCercano ? "SI" : "NO");   // Indica si hay objeto a menos de 15cm
  Serial.print("Sensor evitacion: ");
  Serial.println(estado.sensorEvitacion ? "ACTIVO" : "INACTIVO"); // Estado del sensor infrarrojo
  Serial.print("PIR movimiento: ");
  Serial.println(estado.movimientoDetectado ? "DETECTADO" : "NO"); // Estado del sensor de movimiento
  Serial.println("==========================");
}