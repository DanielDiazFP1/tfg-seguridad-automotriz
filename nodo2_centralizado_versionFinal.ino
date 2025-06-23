#include "DHT.h" 

/*
 * SISTEMA DE SEGURIDAD DEL VEHÍCULO
 */

// =============================================
// CONFIGURACIÓN DEL SISTEMA
// =============================================

// Pines de Sensores
#define PIN_DHT 2
#define TIPO_DHT DHT22
#define PIN_ALIMENTACION_SENSOR_AGUA 7
#define PIN_SENSOR_FUEGO_AO A2
#define PIN_SENSOR_AGUA A3

// Pines de Actuadores
#define PIN_RELE 3
#define PIN_LED_ROJO 12
#define PIN_LED_VERDE 11
#define PIN_LED_AZUL 9

// Configuración de umbrales críticos
struct UmbralesSistema {
    float tempMotorAlerta = 31.0;
    float tempMotorCritica = 34.0;
    float humedadFiltracion = 60.0;
    int nivelFiltracionCritico = 130;
    int nivelFiltracionAlerta = 90;
    int nivelFiltracionLeve = 50;
    int fuegoEmergencia = 100;
    int fuegoAlerta = 350;
    int fuegoPrecaucion = 575;
} umbrales;

// Configuración de temporización
struct ConfiguracionTiempo {
    unsigned long intervaloPrincipal = 2000;
    unsigned long intervaloParpadeo = 500;
    unsigned long tiempoDebounce = 2500; 
    unsigned long duracionPulsoDrenaje = 2000;
    unsigned long pausaPulsoDrenaje = 3000;
    unsigned long duracionTemporal = 5000;
    unsigned long pausaTemporal = 10000;
} config;

// Estados del sistema
enum EstadoSistema {
    NORMAL = 0,
    PRECAUCION = 1,
    ALERTA = 2,
    EMERGENCIA = 3
};

enum ModoRele {
    RELE_OFF = 0,
    RELE_PERMANENTE = 1,
    RELE_PULSOS = 2,
    RELE_TEMPORAL = 3
};

// Estructura para manejo de sensores con filtrado avanzado
struct DatosSensor {
    float valorActual = 0.0;
    float valorFiltrado = 0.0;
    float valorAnterior = 0.0;
    unsigned long ultimaLectura = 0;
    bool errorConsecutivo = false;
    int contadorErrores = 0;
    const int maxErrores = 5;
    
    void actualizarValor(float nuevoValor, float factorFiltro = 0.7) {
        if (!isnan(nuevoValor)) {
            valorAnterior = valorFiltrado;
            valorFiltrado = (factorFiltro * valorFiltrado) + ((1.0 - factorFiltro) * nuevoValor);
            valorActual = nuevoValor;
            ultimaLectura = millis();
            contadorErrores = 0;
            errorConsecutivo = false;
        } else {
            contadorErrores++;
            errorConsecutivo = (contadorErrores >= maxErrores);
        }
    }
    
    bool esValorValido() const {
        return !errorConsecutivo && (millis() - ultimaLectura) < 30000; // 30 segundos max
    }
};

// Variables del sistema
DHT dht(PIN_DHT, TIPO_DHT);
DatosSensor temperatura, humedad;
EstadoSistema estadoActual = NORMAL;
EstadoSistema estadoAnterior = NORMAL;
ModoRele modoReleActual = RELE_OFF;

// Variables de temporización y control
unsigned long tiempoUltimoCambioEstado = 0;
unsigned long tiempoInicioRele = 0;
unsigned long ultimoParpadeo = 0;
unsigned long ultimoReporte = 0;
unsigned long contadorCiclos = 0;

// Variables de estado de actuadores
bool estadoLedParpadeo = false;
bool estadoRele = false;

// Estructura para diagnóstico del sistema
struct DiagnosticoSistema {
    unsigned long tiempoFuncionamiento = 0;
    int reiniciosDetectados = 0;
    int erroresSensores = 0;
    int cambiosEstado = 0;
    
    void incrementarReiniciosLiberarmemoria() {
        reiniciosDetectados++;
    }
    
    void registrarErrorSensor() {
        erroresSensores++;
    }
    
    void registrarCambioEstado() {
        cambiosEstado++;
    }
} diagnostico;

void setup() {
    Serial.begin(9600);
    while (!Serial && millis() < 3000); // Esperar conexión serial máximo 3 segundos
    
    imprimirEncabezadoSistema();
    
    // Inicializar hardware
    inicializarPines();
    inicializarSensores();
    
    // Estado inicial seguro
    establecerEstadoInicial();
    
    Serial.println(F("✅ Sistema inicializado correctamente"));
    Serial.print(F("⏱️ Debounce configurado a: "));
    Serial.print(config.tiempoDebounce);
    Serial.println(F(" ms"));
    Serial.println(F("🔄 Iniciando monitoreo continuo..."));
    
    tiempoUltimoCambioEstado = millis();
    ultimoReporte = millis();
}

void loop() {
    unsigned long tiempoActual = millis();
    contadorCiclos++;
    
    // Actualizar tiempo de funcionamiento
    diagnostico.tiempoFuncionamiento = tiempoActual;
    
    // Lectura y procesamiento de sensores
    procesarSensores();
    
    // Evaluación del estado del sistema con debounce
    evaluarEstadoSistema(tiempoActual);
    
    // Control de actuadores
    controlarActuadores(tiempoActual);
    
    // Reporte periódico (cada 30 segundos o cambio de estado)
    if (tiempoActual - ultimoReporte > 30000 || estadoActual != estadoAnterior) {
        generarReporteSistema();
        ultimoReporte = tiempoActual;
    }
    
    // Intervalo principal
    delay(config.intervaloPrincipal);
}

void imprimirEncabezadoSistema() {
    Serial.println(F("================================================"));
    Serial.println(F("🚗 SISTEMA DE SEGURIDAD VEHICULAR v2.1"));
    Serial.println(F("📊 Nodo Central Inteligente - Respuesta Rápida"));
    Serial.println(F("🔧 Versión: Debounce optimizado (2.5s)"));
    Serial.println(F("================================================"));
}

void inicializarPines() {
    // Configurar pines de sensores
    pinMode(PIN_ALIMENTACION_SENSOR_AGUA, OUTPUT);
    digitalWrite(PIN_ALIMENTACION_SENSOR_AGUA, LOW);
    
    // Configurar pines de actuadores
    pinMode(PIN_RELE, OUTPUT);
    pinMode(PIN_LED_ROJO, OUTPUT);
    pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_LED_AZUL, OUTPUT);
    
    Serial.println(F("🔌 Pines configurados"));
}

void inicializarSensores() {
    dht.begin();
    delay(2000); // Tiempo de estabilización del DHT22
    
    // Lectura inicial para calibrar filtros
    float tempInicial = dht.readTemperature();
    float humInicial = dht.readHumidity();
    
    if (!isnan(tempInicial) && !isnan(humInicial)) {
        temperatura.valorFiltrado = tempInicial;
        humedad.valorFiltrado = humInicial;
        Serial.println(F("🌡️ Sensores DHT22 calibrados"));
    } else {
        Serial.println(F("⚠️ Error inicial en DHT22 - usando valores por defecto"));
        temperatura.valorFiltrado = 25.0;
        humedad.valorFiltrado = 50.0;
        diagnostico.registrarErrorSensor();
    }
}

void establecerEstadoInicial() {
    digitalWrite(PIN_RELE, LOW);
    apagarTodosLeds();
    digitalWrite(PIN_LED_VERDE, HIGH);
    estadoActual = NORMAL;
    modoReleActual = RELE_OFF;
}

void procesarSensores() {
    // Lectura DHT22 con manejo de errores
    float tempRaw = dht.readTemperature();
    float humRaw = dht.readHumidity();
    
    temperatura.actualizarValor(tempRaw);
    humedad.actualizarValor(humRaw);
    
    if (!temperatura.esValorValido() || !humedad.esValorValido()) {
        diagnostico.registrarErrorSensor();
    }
}

int leerSensorAgua() {
    digitalWrite(PIN_ALIMENTACION_SENSOR_AGUA, HIGH);
    delay(100); // Tiempo de estabilización mejorado
    
    // Realizar múltiples lecturas para mayor precisión
    long suma = 0;
    const int numLecturas = 5;
    
    for (int i = 0; i < numLecturas; i++) {
        suma += analogRead(PIN_SENSOR_AGUA);
        delay(10);
    }
    
    digitalWrite(PIN_ALIMENTACION_SENSOR_AGUA, LOW);
    
    return suma / numLecturas;
}

void evaluarEstadoSistema(unsigned long tiempoActual) {
    // Leer todos los sensores
    int nivelAgua = leerSensorAgua();
    int valorFuego = analogRead(PIN_SENSOR_FUEGO_AO);
    
    // Análisis de condiciones críticas
    bool fuegoEmergencia = (valorFuego <= umbrales.fuegoEmergencia);
    bool fuegoAlerta = (valorFuego > umbrales.fuegoEmergencia && valorFuego <= umbrales.fuegoAlerta);
    bool fuegoPrecaucion = (valorFuego > umbrales.fuegoAlerta && valorFuego <= umbrales.fuegoPrecaucion);
    
    bool tempCritica = (temperatura.valorFiltrado > umbrales.tempMotorCritica);
    bool tempAlerta = (temperatura.valorFiltrado > umbrales.tempMotorAlerta && 
                      temperatura.valorFiltrado <= umbrales.tempMotorCritica);
    
    bool aguaCritica = (nivelAgua > umbrales.nivelFiltracionCritico);
    bool aguaAlerta = (nivelAgua > umbrales.nivelFiltracionAlerta && 
                      nivelAgua <= umbrales.nivelFiltracionCritico);
    bool aguaLeve = (nivelAgua > umbrales.nivelFiltracionLeve && 
                    nivelAgua <= umbrales.nivelFiltracionAlerta);
    
    bool humedadAlta = (humedad.valorFiltrado > umbrales.humedadFiltracion);
    
    // Determinar nuevo estado con lógica mejorada
    EstadoSistema nuevoEstado = NORMAL;
    
    // Condiciones de emergencia (máxima prioridad)
    if (fuegoEmergencia || tempCritica || aguaCritica ||
        (fuegoAlerta && tempAlerta) ||
        (fuegoAlerta && aguaAlerta) ||
        (tempAlerta && aguaAlerta && humedadAlta)) {
        nuevoEstado = EMERGENCIA;
    }
    // Condiciones de alerta
    else if (fuegoAlerta || tempAlerta || aguaAlerta || 
             (humedadAlta && (fuegoPrecaucion || aguaLeve || tempAlerta))) {
        nuevoEstado = ALERTA;
    }
    // Condiciones de precaución
    else if (fuegoPrecaucion || aguaLeve || 
             (humedadAlta && temperatura.valorFiltrado > 28.0)) {
        nuevoEstado = PRECAUCION;
    }
    
    // Aplicar debounce para cambios de estado (AHORA 2.5 segundos)
    if (nuevoEstado != estadoActual) {
        if (tiempoActual - tiempoUltimoCambioEstado > config.tiempoDebounce) {
            cambiarEstadoSistema(nuevoEstado);
            tiempoUltimoCambioEstado = tiempoActual;
        }
    } else {
        tiempoUltimoCambioEstado = tiempoActual; // Reset debounce si el estado se mantiene
    }
}

void cambiarEstadoSistema(EstadoSistema nuevoEstado) {
    if (nuevoEstado == estadoActual) return;
    
    estadoAnterior = estadoActual;
    estadoActual = nuevoEstado;
    diagnostico.registrarCambioEstado();
    
    Serial.println(F("\n🔄 === CAMBIO DE ESTADO DETECTADO ==="));
    Serial.print(F("📊 Estado anterior: ")); imprimirNombreEstado(estadoAnterior);
    Serial.print(F("📊 Estado actual: ")); imprimirNombreEstado(estadoActual);
    
    // Configurar actuadores según el nuevo estado
    switch (estadoActual) {
        case EMERGENCIA:
            configurarRele(RELE_PERMANENTE);
            configurarLeds(true, false, false); // Rojo con parpadeo
            Serial.println(F("🚨 ACCIÓN: Corte de emergencia activado"));
            break;
            
        case ALERTA:
            configurarRele(RELE_TEMPORAL);
            configurarLeds(true, true, false); // Amarillo
            Serial.println(F("⚠️ ACCIÓN: Sistema auxiliar activado"));
            break;
            
        case PRECAUCION:
            configurarRele(RELE_OFF);
            configurarLeds(false, false, true); // Azul
            Serial.println(F("🟡 ACCIÓN: Monitoreo intensivo"));
            break;
            
        case NORMAL:
            configurarRele(RELE_OFF);
            configurarLeds(false, true, false); // Verde
            Serial.println(F("✅ ACCIÓN: Operación normal"));
            break;
    }
}

void configurarRele(ModoRele nuevoModo) {
    if (modoReleActual != nuevoModo) {
        modoReleActual = nuevoModo;
        tiempoInicioRele = millis();
        
        switch (nuevoModo) {
            case RELE_OFF:
                digitalWrite(PIN_RELE, LOW);
                estadoRele = false;
                break;
            case RELE_PERMANENTE:
                digitalWrite(PIN_RELE, HIGH);
                estadoRele = true;
                break;
            default:
                // Para modos temporales y pulsos, se maneja en controlarActuadores()
                break;
        }
    }
}

void configurarLeds(bool rojo, bool verde, bool azul) {
    apagarTodosLeds();
    if (rojo) digitalWrite(PIN_LED_ROJO, HIGH);
    if (verde) digitalWrite(PIN_LED_VERDE, HIGH);
    if (azul) digitalWrite(PIN_LED_AZUL, HIGH);
}

void apagarTodosLeds() {
    digitalWrite(PIN_LED_ROJO, LOW);
    digitalWrite(PIN_LED_VERDE, LOW);
    digitalWrite(PIN_LED_AZUL, LOW);
}

void controlarActuadores(unsigned long tiempoActual) {
    // Control de parpadeo en emergencia
    if (estadoActual == EMERGENCIA && 
        tiempoActual - ultimoParpadeo > config.intervaloParpadeo) {
        estadoLedParpadeo = !estadoLedParpadeo;
        if (estadoLedParpadeo) {
            configurarLeds(true, false, false);
        } else {
            configurarLeds(false, false, true);
        }
        ultimoParpadeo = tiempoActual;
    }
    
    // Control de relé según modo
    controlarReleInteligente(tiempoActual);
}

void controlarReleInteligente(unsigned long tiempoActual) {
    unsigned long tiempoTranscurrido = tiempoActual - tiempoInicioRele;
    
    switch (modoReleActual) {
        case RELE_PULSOS:
            if (!estadoRele && tiempoTranscurrido >= config.pausaPulsoDrenaje) {
                digitalWrite(PIN_RELE, HIGH);
                estadoRele = true;
                tiempoInicioRele = tiempoActual;
            } else if (estadoRele && tiempoTranscurrido >= config.duracionPulsoDrenaje) {
                digitalWrite(PIN_RELE, LOW);
                estadoRele = false;
                tiempoInicioRele = tiempoActual;
            }
            break;
            
        case RELE_TEMPORAL:
            if (!estadoRele && tiempoTranscurrido >= config.pausaTemporal) {
                digitalWrite(PIN_RELE, HIGH);
                estadoRele = true;
                tiempoInicioRele = tiempoActual;
            } else if (estadoRele && tiempoTranscurrido >= config.duracionTemporal) {
                digitalWrite(PIN_RELE, LOW);
                estadoRele = false;
                tiempoInicioRele = tiempoActual;
            }
            break;
    }
}

void generarReporteSistema() {
    Serial.println(F("\n📊 === REPORTE DEL SISTEMA ==="));
    
    // Estado general
    Serial.print(F("🚗 Estado: ")); 
    imprimirNombreEstado(estadoActual);
    
    // Datos de sensores con validación
    Serial.print(F("🌡️ Temperatura: "));
    if (temperatura.esValorValido()) {
        Serial.print(temperatura.valorFiltrado, 1);
        Serial.print(F("°C"));
        evaluarUmbralTemperatura();
    } else {
        Serial.print(F("ERROR - Sensor DHT22"));
    }
    Serial.println();
    
    Serial.print(F("💧 Humedad: "));
    if (humedad.esValorValido()) {
        Serial.print(humedad.valorFiltrado, 1);
        Serial.print(F("%"));
        if (humedad.valorFiltrado > umbrales.humedadFiltracion) {
            Serial.print(F(" - ALTA"));
        }
    } else {
        Serial.print(F("ERROR - Sensor DHT22"));
    }
    Serial.println();
    
    // Sensor de agua
    int nivelAgua = leerSensorAgua();
    Serial.print(F("🌊 Nivel agua: "));
    Serial.print(nivelAgua);
    evaluarUmbralAgua(nivelAgua);
    Serial.println();
    
    // Sensor de fuego
    int valorFuego = analogRead(PIN_SENSOR_FUEGO_AO);
    Serial.print(F("🔥 Detector fuego: "));
    Serial.print(valorFuego);
    evaluarUmbralFuego(valorFuego);
    Serial.println();
    
    // Estado del relé
    Serial.print(F("🔌 Relé: "));
    imprimirEstadoRele();
    
    // Diagnóstico del sistema
    Serial.print(F("⏱️ Tiempo funcionamiento: "));
    Serial.print(diagnostico.tiempoFuncionamiento / 1000);
    Serial.println(F(" segundos"));
    
    Serial.print(F("🔄 Ciclos completados: "));
    Serial.println(contadorCiclos);
    
    if (diagnostico.erroresSensores > 0) {
        Serial.print(F("⚠️ Errores sensores: "));
        Serial.println(diagnostico.erroresSensores);
    }
    
    Serial.println(F("================================"));
}

void imprimirNombreEstado(EstadoSistema estado) {
    switch (estado) {
        case NORMAL: Serial.println(F("✅ NORMAL")); break;
        case PRECAUCION: Serial.println(F("🟡 PRECAUCIÓN")); break;
        case ALERTA: Serial.println(F("⚠️ ALERTA")); break;
        case EMERGENCIA: Serial.println(F("🚨 EMERGENCIA")); break;
    }
}

void evaluarUmbralTemperatura() {
    if (temperatura.valorFiltrado > umbrales.tempMotorCritica) {
        Serial.print(F(" - CRÍTICO"));
    } else if (temperatura.valorFiltrado > umbrales.tempMotorAlerta) {
        Serial.print(F(" - ALERTA"));
    }
}

void evaluarUmbralAgua(int nivel) {
    if (nivel > umbrales.nivelFiltracionCritico) {
        Serial.print(F(" - CRÍTICO"));
    } else if (nivel > umbrales.nivelFiltracionAlerta) {
        Serial.print(F(" - ALERTA"));
    } else if (nivel > umbrales.nivelFiltracionLeve) {
        Serial.print(F(" - LEVE"));
    }
}

void evaluarUmbralFuego(int valor) {
    if (valor <= umbrales.fuegoEmergencia) {
        Serial.print(F(" - EMERGENCIA"));
    } else if (valor <= umbrales.fuegoAlerta) {
        Serial.print(F(" - ALERTA"));
    } else if (valor <= umbrales.fuegoPrecaucion) {
        Serial.print(F(" - PRECAUCIÓN"));
    }
}

void imprimirEstadoRele() {
    switch (modoReleActual) {
        case RELE_OFF: Serial.println(F("Desactivado")); break;
        case RELE_PERMANENTE: Serial.println(F("CORTE EMERGENCIA")); break;
        case RELE_PULSOS: Serial.println(F("Pulsos drenaje")); break;
        case RELE_TEMPORAL: Serial.println(F("Activación temporal")); break;
    }
}