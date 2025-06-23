#include <Arduino.h>

// =============================================
// DEFINICIÓN DE PINES DEL SISTEMA
// =============================================
const int PIN_SENSOR_CAMBIO_CARRIL = A5;
const int PIN_ZUMBADOR = 9;
const int PIN_DETECCION_CONDUCTOR_ANALOGICO = A0;
const int PIN_ULTRASONICO_DISPARADOR = 2;
const int PIN_ULTRASONICO_ECO = 3;
const int PIN_LASER = 12;

// =============================================
// CONSTANTES DEL SISTEMA
// =============================================
const int UMBRAL_DISTANCIA_CRITICA = 20;
const int UMBRAL_DISTANCIA_PRECAUCION = 30;
const int UMBRAL_CONDUCTOR = 300;  // Valor para detectar conductor
const int UMBRAL_CARRIL_NEGRO = 400;  // Valor para superficie negra (carretera)
const int UMBRAL_CARRIL_BLANCO = 700; // Valor para línea blanca
const unsigned long TIEMPO_ESPERA_ULTRASONICO = 30000UL;
const int RETARDO_BUCLE = 100;
const int DURACION_INTERMITENCIA = 200;
const unsigned long INTERVALO_MEDICION = 50UL;
const unsigned long INTERVALO_CARRIL = 100UL; // Para evitar falsos positivos

// =============================================
// VARIABLES GLOBALES
// =============================================
unsigned long ultimaMedicionUltrasonica = 0;
unsigned long ultimaLecturaCarril = 0;
bool estadoCarrilAnterior = true; // true = negro, false = blanco
bool cambioCarrilConfirmado = false;
unsigned long tiempoUltimoCambioCarril = 0;

// =============================================
// ESTRUCTURA PARA ORGANIZAR EL ESTADO DEL SISTEMA
// =============================================
struct EstadoSistema {
  bool cambioCarrilDetectado;
  bool conductorPresente;
  int distanciaObjeto;
  bool objetoCercano;
};

// Variables para evitar spam de mensajes
char ultimoMensaje[100] = "";
char ultimoMensajeInfo[500] = "";

// =============================================
// CONFIGURACIÓN INICIAL DEL SISTEMA
// =============================================
void setup() {
  Serial.begin(9600); // Velocidad de comunicación

  // Configuración de pines
  pinMode(PIN_LASER, OUTPUT);
  pinMode(PIN_SENSOR_CAMBIO_CARRIL, INPUT);
  pinMode(PIN_DETECCION_CONDUCTOR_ANALOGICO, INPUT);
  pinMode(PIN_ULTRASONICO_DISPARADOR, OUTPUT);
  pinMode(PIN_ULTRASONICO_ECO, INPUT);
  pinMode(PIN_ZUMBADOR, OUTPUT);

  // Estado inicial seguro
  digitalWrite(PIN_ULTRASONICO_DISPARADOR, LOW);
  digitalWrite(PIN_LASER, LOW);
  noTone(PIN_ZUMBADOR);

  Serial.println("=== SISTEMA DE ASISTENCIA AL CONDUCTOR ===");
  Serial.println("Sistema iniciado correctamente");
  Serial.println("Envie 'd' para informacion de debug");
  Serial.println("==========================================");
  delay(2000);
}

// =============================================
// BUCLE PRINCIPAL DEL PROGRAMA
// =============================================
void loop() {
  EstadoSistema estado = leerSensores();

  // Debug: envía 'd' por serial para información detallada
  if (Serial.available() && Serial.read() == 'd') {
    imprimirInfoDebug(estado);
  }

  procesarLogicaAlarma(estado);
  delay(RETARDO_BUCLE);
}

// =============================================
// FUNCIÓN PARA LEER TODOS LOS SENSORES
// =============================================
EstadoSistema leerSensores() {
  EstadoSistema estado;

  // Lectura del sensor de cambio de carril (detección negro -> blanco)
  estado.cambioCarrilDetectado = detectarCambioCarril();

  // Lectura del sensor de detección del conductor
  int valorSensorConductor = analogRead(PIN_DETECCION_CONDUCTOR_ANALOGICO);
  estado.conductorPresente = (valorSensorConductor < UMBRAL_CONDUCTOR);

  // Lectura del sensor ultrasónico
  estado.distanciaObjeto = leerDistanciaUltrasonica();
  estado.objetoCercano = (estado.distanciaObjeto > 0 && estado.distanciaObjeto < UMBRAL_DISTANCIA_CRITICA);

  return estado;
}

// =============================================
// FUNCIÓN PARA DETECTAR CAMBIO DE CARRIL (NEGRO -> BLANCO)
// =============================================
bool detectarCambioCarril() {
  // Limita la frecuencia de lectura para estabilidad, evitando lecturas demasiado rápidas
  // Si no ha pasado suficiente tiempo desde la última lectura, devuelve el estado actual confirmado.
  if (millis() - ultimaLecturaCarril < INTERVALO_CARRIL) {
    return cambioCarrilConfirmado;
  }

  // Lee el valor analógico del sensor de cambio de carril.
  int valorCarril = analogRead(PIN_SENSOR_CAMBIO_CARRIL);
  bool estadoActual; // Variable para almacenar el estado actual del carril (negro o blanco).
  
  // Determina si el sensor está sobre una superficie negra (carretera) o blanca (línea).
  // Los umbrales UMBRAL_CARRIL_NEGRO y UMBRAL_CARRIL_BLANCO definen estos rangos.
  if (valorCarril < UMBRAL_CARRIL_NEGRO) {
    estadoActual = true; // 'true' representa superficie negra (carretera).
  } else if (valorCarril > UMBRAL_CARRIL_BLANCO) {
    estadoActual = false; // 'false' representa superficie blanca (línea).
  } else {
    // Si el valor está entre los umbrales (zona intermedia), mantiene el estado anterior
    // para evitar fluctuaciones y reinicia el temporizador de última lectura.
    ultimaLecturaCarril = millis();
    return cambioCarrilConfirmado;
  }

  // Detecta una transición de negro a blanco.
  // Si el estado anterior era negro ('!estadoCarrilAnterior') y el actual es blanco ('estadoActual'),
  // se confirma el cambio de carril y se registra el tiempo.
  if (!estadoCarrilAnterior && estadoActual) {
    cambioCarrilConfirmado = true;
    tiempoUltimoCambioCarril = millis();
  }

  // Reinicia la bandera de 'cambioCarrilConfirmado' después de un tiempo determinado (3 segundos).
  // Esto evita que una detección de cambio de carril permanezca activa indefinidamente.
  if (cambioCarrilConfirmado && (millis() - tiempoUltimoCambioCarril > 3000)) {
    cambioCarrilConfirmado = false;
  }

  // Actualiza el estado del carril para la próxima comparación y el tiempo de la última lectura.
  estadoCarrilAnterior = estadoActual;
  ultimaLecturaCarril = millis();
  
  // Devuelve si se ha confirmado un cambio de carril.
  return cambioCarrilConfirmado;
}

// =============================================
// FUNCIÓN PARA MEDIR DISTANCIA CON SENSOR ULTRASÓNICO
// =============================================
int leerDistanciaUltrasonica() {
  // Evita mediciones muy frecuentes
  if (millis() - ultimaMedicionUltrasonica < INTERVALO_MEDICION) {
    return -1;
  }

  // Secuencia para activar el sensor HC-SR04
  digitalWrite(PIN_ULTRASONICO_DISPARADOR, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_ULTRASONICO_DISPARADOR, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_ULTRASONICO_DISPARADOR, LOW);

  // Mide el tiempo con timeout
  long duracion = pulseIn(PIN_ULTRASONICO_ECO, HIGH, TIEMPO_ESPERA_ULTRASONICO);
  ultimaMedicionUltrasonica = millis();

  if (duracion == 0) {
    return -1; // Error de timeout
  }

  // Conversión a centímetros
  int distancia = duracion / 59;

  // Filtro de valores válidos (2cm - 400cm para HC-SR04)
  if (distancia < 2 || distancia > 400) {
    return -1;
  }

  return distancia;
}

// =============================================
// LÓGICA PRINCIPAL DE PROCESAMIENTO DE ALARMAS
// =============================================
void procesarLogicaAlarma(EstadoSistema estado) {
  // Reinicia alarmas
  digitalWrite(PIN_LASER, LOW);
  noTone(PIN_ZUMBADOR);

  // CONDICIÓN PRINCIPAL: Si hay conductor, NO se activa ninguna alarma
  if (estado.conductorPresente) {
    establecerEstadoSeguro("CONDUCTOR DETECTADO - Sistema en monitoreo");
    imprimirInformacionSegura(estado);
    return;
  }

  // === ALARMAS CUANDO NO HAY CONDUCTOR ===
  
  // CONDICIÓN 1: ALARMA CRÍTICA - Objeto cercano + Cambio carril + Sin conductor
  if (estado.objetoCercano && estado.cambioCarrilDetectado) {
    alarmaMaxima("PELIGRO EXTREMO! Objeto cercano y cambio de carril sin conductor!");
  }
  // CONDICIÓN 2: ALARMA ALTA - Solo objeto cercano + Sin conductor  
  else if (estado.objetoCercano) {
    alarmaPorObjeto("PELIGRO! Objeto cercano detectado sin conductor!");
  }
  // CONDICIÓN 3: ALARMA MEDIA - Solo cambio carril + Sin conductor
  else if (estado.cambioCarrilDetectado) {
    alarmaPorCarril("ATENCION! Cambio de carril detectado sin conductor!");
  }
  // CONDICIÓN 4: ALARMA BÁSICA - Sin conductor (pero sin otros peligros)
  else {
    alarmaSinConductor("ALERTA! Conductor no detectado - Tome el volante!");
  }
}

// =============================================
// FUNCIONES DE CONTROL DE ALARMAS
// =============================================

void establecerEstadoSeguro(const char* mensaje) {
  if (strcmp(mensaje, ultimoMensaje) != 0) {
    Serial.println(mensaje);
    strcpy(ultimoMensaje, mensaje);
  }
}

void alarmaMaxima(const char* mensaje) {
  // Alarma crítica: Tono muy agudo + Láser intermitente rápido
  if (strcmp(mensaje, ultimoMensaje) != 0) {
    Serial.println(mensaje);
    strcpy(ultimoMensaje, mensaje);
  }
  
  // Patrón: 3 tonos agudos + láser intermitente muy rápido
  for (int i = 0; i < 3; i++) {
    tone(PIN_ZUMBADOR, 1000); // Tono muy agudo
    digitalWrite(PIN_LASER, HIGH);
    delay(100);
    noTone(PIN_ZUMBADOR);
    digitalWrite(PIN_LASER, LOW);
    delay(100);
  }
}

void alarmaPorObjeto(const char* mensaje) {
  // Alarma por objeto: Tono medio + Láser intermitente normal
  if (strcmp(mensaje, ultimoMensaje) != 0) {
    Serial.println(mensaje);
    strcpy(ultimoMensaje, mensaje);
  }
  
  // Patrón: 2 tonos medios + láser intermitente
  for (int i = 0; i < 2; i++) {
    tone(PIN_ZUMBADOR, 600); // Tono medio
    digitalWrite(PIN_LASER, HIGH);
    delay(DURACION_INTERMITENCIA);
    noTone(PIN_ZUMBADOR);
    digitalWrite(PIN_LASER, LOW);
    delay(DURACION_INTERMITENCIA);
  }
}

void alarmaPorCarril(const char* mensaje) {
  // Alarma por carril: Tono bajo + Láser intermitente lento
  if (strcmp(mensaje, ultimoMensaje) != 0) {
    Serial.println(mensaje);
    strcpy(ultimoMensaje, mensaje);
  }
  
  // Patrón: 1 tono largo bajo + láser intermitente lento
  tone(PIN_ZUMBADOR, 400); // Tono bajo
  digitalWrite(PIN_LASER, HIGH);
  delay(400);
  noTone(PIN_ZUMBADOR);
  digitalWrite(PIN_LASER, LOW);
  delay(400);
}

void alarmaSinConductor(const char* mensaje) {
  // Alarma básica: Solo tono suave sin láser
  if (strcmp(mensaje, ultimoMensaje) != 0) {
    Serial.println(mensaje);
    strcpy(ultimoMensaje, mensaje);
  }
  
  // Patrón: Tono grave intermitente
  tone(PIN_ZUMBADOR, 100);
  delay(150);
  noTone(PIN_ZUMBADOR);
  delay(150);
}

// =============================================
// FUNCIÓN PARA MOSTRAR INFORMACIÓN EN MODO SEGURO
// =============================================
void imprimirInformacionSegura(EstadoSistema estado) {
  char mensajeBuffer[500];
  strcpy(mensajeBuffer, "\n--- Estado del Vehiculo (Modo Seguro) ---\n");

  // Información de cambio de carril
  if (estado.cambioCarrilDetectado) {
    strcat(mensajeBuffer, "- Cambio de carril: DETECTADO\n");
  } else {
    strcat(mensajeBuffer, "- Cambio de carril: NO DETECTADO\n");
  }

  // Información de distancia
  if (estado.distanciaObjeto > 0) {
    char distanciaStr[100];
    if (estado.distanciaObjeto < UMBRAL_DISTANCIA_CRITICA) {
      sprintf(distanciaStr, "- Distancia: %d cm (OBJETO CERCANO)\n", estado.distanciaObjeto);
    } else if (estado.distanciaObjeto < UMBRAL_DISTANCIA_PRECAUCION) {
      sprintf(distanciaStr, "- Distancia: %d cm (Precaucion)\n", estado.distanciaObjeto);
    } else {
      sprintf(distanciaStr, "- Distancia: %d cm (Segura)\n", estado.distanciaObjeto);
    }
    strcat(mensajeBuffer, distanciaStr);
  } else {
    strcat(mensajeBuffer, "- Distancia: No disponible\n");
  }

  strcat(mensajeBuffer, "- Conductor: PRESENTE - Sistema OK\n");
  strcat(mensajeBuffer, "==========================================\n");

  // Solo imprime si el mensaje ha cambiado
  if (strcmp(mensajeBuffer, ultimoMensajeInfo) != 0) {
    Serial.print(mensajeBuffer);
    strcpy(ultimoMensajeInfo, mensajeBuffer);
  }
}

// =============================================
// FUNCIÓN DE DEBUG 
// =============================================
void imprimirInfoDebug(EstadoSistema estado) {
  Serial.println("\n========== DEBUG ==========");
  
  // Valores raw de sensores
  int valorConductor = analogRead(PIN_DETECCION_CONDUCTOR_ANALOGICO);
  int valorCarril = analogRead(PIN_SENSOR_CAMBIO_CARRIL);
  
  Serial.print("Sensor conductor (raw): ");
  Serial.print(valorConductor);
  Serial.print(" | Umbral: ");
  Serial.print(UMBRAL_CONDUCTOR);
  Serial.print(" | Estado: ");
  Serial.println(estado.conductorPresente ? "PRESENTE" : "AUSENTE");
  
  Serial.print("Sensor carril (raw): ");
  Serial.print(valorCarril);
  Serial.print(" | Estado superficie: ");
  if (valorCarril < UMBRAL_CARRIL_NEGRO) {
    Serial.print("NEGRO (carretera)");
  } else if (valorCarril > UMBRAL_CARRIL_BLANCO) {
    Serial.print("BLANCO (linea)");
  } else {
    Serial.print("INTERMEDIO");
  }
  Serial.print(" | Cambio detectado: ");
  Serial.println(estado.cambioCarrilDetectado ? "SI" : "NO");
  
  Serial.print("Distancia ultrasonica: ");
  if (estado.distanciaObjeto != -1) {
    Serial.print(estado.distanciaObjeto);
    Serial.print(" cm | Objeto cercano: ");
    Serial.println(estado.objetoCercano ? "SI" : "NO");
  } else {
    Serial.println("ERROR DE LECTURA");
  }
  
  Serial.print("Estado del sistema: ");
  if (estado.conductorPresente) {
    Serial.println("MODO SEGURO (Conductor presente)");
  } else {
    Serial.println("MODO ALARMA (Sin conductor)");
  }
  
  Serial.println("=====================================\n");
}