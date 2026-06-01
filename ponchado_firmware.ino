// ============================================================
//
//   SISTEMA DE PONCHADO AUTOMATICO
//   Proyecto de Graduacion
//   Marco Flores - 2019232427
//
//   Plataforma : Teensy 4.1
//
//   Estados:
//     HOMING        : referencia de mesa y agujas
//     ESPERANDO     : aguarda START
//     MOVIENDO_MESA : desplaza al siguiente agujero con overlap
//     PERFORANDO    : completa ciclo de ponchado
//     FIN           : proceso completo, regresa y espera
//     ERROR         : parada de seguridad, espera RESET
//
//   Comandos Serial:
//     START   -> inicia desde ESPERANDO
//     STOP    -> va a ERROR
//     RESET   -> desde ERROR rehace homing completo
//     STATUS  -> muestra estado actual
//     PARTEn  -> selecciona receta n (1-30)
//
// ============================================================

#include <Arduino.h>

// ============================================================
//   PINES
// ============================================================

const int ENA1 = 1;
const int DIR1 = 2;
const int PUL1 = 3;

const int ENA2 = 5;
const int DIR2 = 6;
const int PUL2 = 7;

const int PIN_MESA_FINAL    = 8;
const int PIN_MESA_INICIO   = 9;
const int PIN_AGUJAS_ABAJO  = 10;
const int PIN_AGUJAS_ARRIBA = 11;

// ============================================================
//   DIRECCIONES
// ============================================================

#define MESA_HACIA_INICIO   LOW
#define MESA_HACIA_FINAL    HIGH
#define AGUJAS_BAJAR        LOW
#define AGUJAS_SUBIR        HIGH

// ============================================================
//   MECANICA
// ============================================================

const float PASOS_POR_MM_MESA   = 400.0f;
const float PASOS_POR_MM_AGUJAS = 400.0f;

// ============================================================
//   VELOCIDADES
// ============================================================

namespace Vel {
    const int HOMING_RAPIDO = 60;
    const int HOMING_LENTO  = 400;
    const int TRABAJO_MESA  = 60;
    const int AGUJAS_NORMAL = 40;
    const int AGUJAS_LENTO  = 200;
    const int RAMPA_INICIO  = 800;
}

// ============================================================
//   PARAMETROS DE PROCESO
// ============================================================

const int   MM_EXTRA_PERFORACION = 4.0f;
const int   RETROCESO_HOMING     = 1200;
const int   DEBOUNCE_MS          = 2;

float calcularExtraPerforacion(uint8_t french) {
    switch (french) {
        case 6: return 4.5f;
        case 5: return 4.6f;
        case 4: return 4.7f;
        default: return MM_EXTRA_PERFORACION;
    }
}

const long  PASOS_OVERLAP = 600;
const float MM_MAX_AGUJAS = 10.0f;

// ============================================================
//   RECETAS
// ============================================================

struct Receta {
    const char* nombre;
    int totalAgujeros;
    uint8_t french;
    float offsetInicial;
    const float* espaciados;
};

// ---- ESPACIADOS ----
// Nomenclatura: ESP_Pn = espaciados de PARTE n
const float ESP_P1[]  = {5,5,5,5,5,5,5,5,5};
const float ESP_P2[]  = {2,5,2,5,2,5,2,5,2};
const float ESP_P3[]  = {2,5,2,5,2,5,2,5,2};
const float ESP_P4[]  = {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2};
const float ESP_P5[]  = {2,2,2,2,2,2,2,2,2};
const float ESP_P6[]  = {2,3,1,3,1,3,1,3,1};
const float ESP_P7[]  = {4,4,4,4,4,4,4,4,4};
const float ESP_P8[]  = {2,8,2,8,2,8,2,8,2};
const float ESP_P9[]  = {2,2,2,2,2,2,2,2,2};
const float ESP_P10[] = {5,5,5};
const float ESP_P11[] = {5,5,5};
const float ESP_P12[] = {2,5,2,5,2,5,2,5,2};
const float ESP_P13[] = {2,5,2,5,2,5,2,5,2};
const float ESP_P14[] = {2,5,2};
const float ESP_P15[] = {5,5,5};
const float ESP_P16[] = {5,5,5};
const float ESP_P17[] = {2,5,2};
const float ESP_P18[] = {2,5,2};
const float ESP_P19[] = {2,5,2};
const float ESP_P20[] = {5,5,5};
const float ESP_P21[] = {2,5,2,5,2,5,2,5,2};
const float ESP_P22[] = {2,30,3,3,3,3,3,3,3};
const float ESP_P23[] = {2,2,2,2,2,2,2};
const float ESP_P24[] = {2,5,2,5,2,5,2};
const float ESP_P25[] = {2,2,2,2,2,2,2,2,2};
const float ESP_P26[] = {2,2,2,2,2,2,2,2,2};
const float ESP_P27[] = {2,5,2,5,2,5,2,5,2};
const float ESP_P28[] = {2,2,2,2,2,2,2};
const float ESP_P29[] = {2,5,2,5,2,5,2,5,2};
const float ESP_P30[] = {2,5,2,5,2,5,2,5,2};

// ---- RECETAS (totalAgujeros = bandas, offsetInicial = L en ponchador) ----
//                nombre      holes  Fr  L    espaciados
const Receta PARTE1  = {"PARTE1",  10, 6,  9, ESP_P1};   // 10B F6 L=9  esp=5mm
const Receta PARTE2  = {"PARTE2",  10, 6, 35, ESP_P2};   // 10B F6 L=35 esp=2/5
const Receta PARTE3  = {"PARTE3",  10, 6,  9, ESP_P3};   // 10B F6 L=9  esp=2/5
const Receta PARTE4  = {"PARTE4",  20, 6, 18, ESP_P4};   // 20B F6 L=18 esp=2mm
const Receta PARTE5  = {"PARTE5",  10, 6, 18, ESP_P5};   // 10B F6 L=18 esp=2mm
const Receta PARTE6  = {"PARTE6",  10, 6, 16, ESP_P6};   // 10B F6 L=16 variable
const Receta PARTE7  = {"PARTE7",  10, 5, 19, ESP_P7};   // 10B F5 L=19 esp=4mm
const Receta PARTE8  = {"PARTE8",  10, 5, 18, ESP_P8};   // 10B F5 L=18 esp=2/8
const Receta PARTE9  = {"PARTE9",  10, 5, 21, ESP_P9};   // 10B F5 L=21 esp=2mm
const Receta PARTE10 = {"PARTE10",  4, 5, 19, ESP_P10};  // 4B  F5 L=19 esp=5mm
const Receta PARTE11 = {"PARTE11",  4, 4, 17, ESP_P11};  // 4B  F4 L=17 esp=5mm
const Receta PARTE12 = {"PARTE12", 10, 6, 28, ESP_P12};  // 10B F6 L=28 esp=2/5
const Receta PARTE13 = {"PARTE13", 10, 5, 37, ESP_P13};  // 10B F5 L=37 esp=2/5
const Receta PARTE14 = {"PARTE14",  4, 5,  7, ESP_P14};  // 4B  F5 L=7  esp=2/5
const Receta PARTE15 = {"PARTE15",  4, 6, 37, ESP_P15};  // 4B  F6 L=37 esp=5mm
const Receta PARTE16 = {"PARTE16",  4, 6,  9, ESP_P16};  // 4B  F6 L=9  esp=5mm
const Receta PARTE17 = {"PARTE17",  4, 6, 37, ESP_P17};  // 4B  F6 L=37 esp=2/5
const Receta PARTE18 = {"PARTE18",  4, 6,  9, ESP_P18};  // 4B  F6 L=9  esp=2/5
const Receta PARTE19 = {"PARTE19",  4, 5, 35, ESP_P19};  // 4B  F5 L=35 esp=2/5
const Receta PARTE20 = {"PARTE20",  4, 5, 35, ESP_P20};  // 4B  F5 L=35 esp=5mm
const Receta PARTE21 = {"PARTE21", 10, 5, 18, ESP_P21};  // 10B F5 L=18 esp=2/5
const Receta PARTE22 = {"PARTE22", 10, 5, 37, ESP_P22};  // 10B F5 L=37 variable
const Receta PARTE23 = {"PARTE23",  8, 6, 34, ESP_P23};  // 8B  F6 L=34 esp=2mm
const Receta PARTE24 = {"PARTE24",  8, 6, 10, ESP_P24};  // 8B  F6 L=10 esp=2/5
const Receta PARTE25 = {"PARTE25", 10, 6, 13, ESP_P25};  // 10B F6 L=13 esp=2mm
const Receta PARTE26 = {"PARTE26", 10, 4, 41, ESP_P26};  // 10B F4 L=41 esp=2mm
const Receta PARTE27 = {"PARTE27", 10, 4, 19, ESP_P27};  // 10B F4 L=19 esp=2/5
const Receta PARTE28 = {"PARTE28",  8, 5, 36, ESP_P28};  // 8B  F5 L=36 esp=2mm
const Receta PARTE29 = {"PARTE29", 10, 6,  8, ESP_P29};  // 10B F6 L=8  esp=2/5
const Receta PARTE30 = {"PARTE30", 10, 5, 18, ESP_P30};  // 10B F5 L=18 esp=2/5 (num.parte alt.)

// Lista de todas las recetas
const Receta* recetas[] = {
    &PARTE1, &PARTE2, &PARTE3, &PARTE4, &PARTE5,
    &PARTE6, &PARTE7, &PARTE8, &PARTE9, &PARTE10,
    &PARTE11,&PARTE12,&PARTE13,&PARTE14,&PARTE15,
    &PARTE16,&PARTE17,&PARTE18,&PARTE19,&PARTE20,
    &PARTE21,&PARTE22,&PARTE23,&PARTE24,&PARTE25,
    &PARTE26,&PARTE27,&PARTE28,&PARTE29,
    &PARTE30
};

const int NUM_RECETAS = sizeof(recetas)/sizeof(recetas[0]);

// Receta activa por defecto
const Receta* recetaActiva = &PARTE1;

// ============================================================
//   ESTADOS
// ============================================================

enum class Estado {
    HOMING,
    ESPERANDO,
    MOVIENDO_MESA,
    PERFORANDO,
    FIN,
    ERROR
};

// ============================================================
//   VARIABLES GLOBALES
// ============================================================

Estado estadoActual       = Estado::HOMING;
bool   agujasEnZonaSegura = false;
bool   stopSolicitado     = false;
String bufferSerial       = "";
int    agujeroActual      = 0;

// ============================================================
//   PROTOTIPOS
// ============================================================

void log(const char* msg);
void log(const String& msg);
void irAError(const char* msg);
void setDirMesa(bool dir);
void setDirAgujas(bool dir);
void deshabilitarTodo();

// ============================================================
//   SENSORES
// ============================================================

bool mesaInicio()   { return digitalRead(PIN_MESA_INICIO)   == HIGH; }
bool mesaFinal()    { return digitalRead(PIN_MESA_FINAL)    == HIGH; }
bool agujasAbajo()  { return digitalRead(PIN_AGUJAS_ABAJO)  == HIGH; }
bool agujasArriba() { return digitalRead(PIN_AGUJAS_ARRIBA) == HIGH; }

// ============================================================
//   MOTORES
// ============================================================

inline void stepMotor(int pin, int delayUs) {
    digitalWrite(pin, HIGH);
    delayMicroseconds(delayUs);
    digitalWrite(pin, LOW);
    delayMicroseconds(delayUs);
}

void setDirMesa(bool dir) {
    digitalWrite(DIR1, dir);
    delayMicroseconds(5);
}

void setDirAgujas(bool dir) {
    digitalWrite(DIR2, dir);
    delayMicroseconds(5);
}

void deshabilitarTodo() {
    digitalWrite(ENA1, HIGH);
    digitalWrite(ENA2, HIGH);
}

// ============================================================
//   LOG
// ============================================================

void log(const char* msg) {
    Serial.print("[INFO]  ");
    Serial.println(msg);
}

void log(const String& msg) { log(msg.c_str()); }

// ============================================================
//   LIBERACION DE AGUJAS DESDE ABAJO
// ============================================================

bool liberarAgujasDesdeAbajo() {
    log("[RECOVERY] Liberando agujas desde limite inferior...");
    digitalWrite(ENA2, LOW);
    setDirAgujas(AGUJAS_SUBIR);
    long contador = 0;
    long timeout  = (long)(MM_MAX_AGUJAS * PASOS_POR_MM_AGUJAS);
    while (agujasAbajo()) {
        if (++contador > timeout) {
            irAError("No se pudo liberar sensor inferior de agujas");
            return false;
        }
        stepMotor(PUL2, Vel::AGUJAS_LENTO);
    }
    log("[RECOVERY] Sensor inferior liberado");
    return true;
}

// ============================================================
//   IR A ERROR
// ============================================================

void irAError(const char* msg) {
    Serial.print("[ERROR] ");
    Serial.println(msg);
    if (agujasAbajo()) {
        liberarAgujasDesdeAbajo();
        agujasEnZonaSegura = true;
    } else {
        deshabilitarTodo();
        agujasEnZonaSegura = false;
    }
    Serial.println("        Envie RESET para reiniciar.");
    estadoActual = Estado::ERROR;
}

// ============================================================
//   SEGURIDAD GLOBAL
// ============================================================

void verificarSeguridadGlobal() {
    if (agujasAbajo()) {
        log("[SEGURIDAD] Limite inferior detectado fuera de ciclo");
        liberarAgujasDesdeAbajo();
        irAError("LIMITE INFERIOR AGUJAS - parada de emergencia");
    }
}

// ============================================================
//   HOMING MESA
// ============================================================

bool homingMesa() {
    log("=== HOMING MESA ===");
    if (!agujasEnZonaSegura) {
        irAError("Agujas deben estar en zona segura para hacer homing de mesa");
        return false;
    }
    if (mesaInicio()) {
        log("Dentro del sensor, saliendo...");
        setDirMesa(MESA_HACIA_FINAL);
        while (mesaInicio()) stepMotor(PUL1, Vel::HOMING_RAPIDO);
        log("Sensor liberado");
    }
    log("Buscando switch...");
    setDirMesa(MESA_HACIA_INICIO);
    long contador = 0;
    while (true) {
        if (mesaInicio()) {
            delay(DEBOUNCE_MS);
            if (mesaInicio()) { log("Switch detectado"); break; }
        }
        if (++contador > 500000L) {
            irAError("Timeout homing mesa - switch no encontrado");
            return false;
        }
        stepMotor(PUL1, Vel::HOMING_RAPIDO);
    }
    setDirMesa(MESA_HACIA_FINAL);
    for (int i = 0; i < RETROCESO_HOMING; i++) stepMotor(PUL1, Vel::HOMING_RAPIDO);
    log("Aproximacion lenta...");
    setDirMesa(MESA_HACIA_INICIO);
    contador = 0;
    while (true) {
        if (mesaInicio()) {
            delay(DEBOUNCE_MS);
            if (mesaInicio()) { log("Homing mesa OK"); break; }
        }
        if (++contador > 300000L) {
            irAError("Timeout homing mesa lento");
            return false;
        }
        stepMotor(PUL1, Vel::HOMING_LENTO);
    }
    return true;
}

// ============================================================
//   HOMING AGUJAS
// ============================================================

bool homingAgujas() {
    log("=== HOMING AGUJAS ===");
    if (agujasArriba()) {
        log("Dentro del sensor, saliendo...");
        setDirAgujas(AGUJAS_BAJAR);
        while (agujasArriba()) {
            if (agujasAbajo()) { irAError("Limite inferior en homing agujas"); return false; }
            stepMotor(PUL2, Vel::AGUJAS_LENTO);
        }
        log("Sensor liberado");
    }
    log("Buscando sensor superior...");
    setDirAgujas(AGUJAS_SUBIR);
    long contador = 0;
    long timeoutBusqueda = (long)(MM_MAX_AGUJAS * PASOS_POR_MM_AGUJAS * 2);
    while (true) {
        if (agujasArriba()) {
            delay(DEBOUNCE_MS);
            if (agujasArriba()) { log("Sensor detectado"); break; }
        }
        if (agujasAbajo()) { irAError("Limite inferior durante busqueda en homing"); return false; }
        if (++contador > timeoutBusqueda) {
            agujasEnZonaSegura = false;
            irAError("Timeout homing agujas");
            return false;
        }
        stepMotor(PUL2, Vel::AGUJAS_NORMAL);
    }
    setDirAgujas(AGUJAS_SUBIR);
    for (int i = 0; i < 800; i++) {
        if (agujasAbajo()) {
            agujasEnZonaSegura = false;
            irAError("Limite inferior en retroceso homing agujas");
            return false;
        }
        stepMotor(PUL2, Vel::AGUJAS_LENTO);
    }
    log("Ajuste fino...");
    setDirAgujas(AGUJAS_SUBIR);
    contador = 0;
    while (true) {
        if (agujasArriba()) {
            delay(DEBOUNCE_MS);
            if (agujasArriba()) { log("Homing agujas OK"); break; }
        }
        if (agujasAbajo()) {
            agujasEnZonaSegura = false;
            irAError("Limite inferior en ajuste fino homing agujas");
            return false;
        }
        if (++contador > timeoutBusqueda) {
            agujasEnZonaSegura = false;
            irAError("Timeout homing agujas lento");
            return false;
        }
        stepMotor(PUL2, Vel::HOMING_LENTO);
    }
    agujasEnZonaSegura = true;
    return true;
}

// ============================================================
//   MOVER MESA
// ============================================================

bool moverMesa(float mm) {
    if (!agujasEnZonaSegura) {
        irAError("Agujas no estan en zona segura, no se puede mover mesa");
        return false;
    }
    long totalPasos = (long)(mm * PASOS_POR_MM_MESA);
    if (totalPasos <= 0) return true;
    log(String("Moviendo ") + String(mm, 1) + " mm");
    setDirMesa(MESA_HACIA_FINAL);
    long pasosRampa  = totalPasos / 10;
    long inicioConst = pasosRampa;
    long finConst    = totalPasos - pasosRampa;
    long pasoOverlap   = totalPasos - PASOS_OVERLAP;
    bool usarOverlap   = (PASOS_OVERLAP >= 400);
    if (pasoOverlap < 0) pasoOverlap = 0;
    bool overlapActivo = false;
    for (long i = 0; i < totalPasos; i++) {
        if (agujasAbajo()) {
            deshabilitarTodo();
            irAError("Limite inferior agujas durante movimiento de mesa");
            return false;
        }
        if (mesaFinal()) {
            deshabilitarTodo();
            irAError("Limite final de mesa activado - revisar receta");
            return false;
        }
        if (stopSolicitado) {
            irAError("Proceso detenido por STOP");
            return false;
        }
        if (usarOverlap && !overlapActivo && i >= pasoOverlap) {
            setDirAgujas(AGUJAS_BAJAR);
            agujasEnZonaSegura = false;
            overlapActivo      = true;
        }
        if (overlapActivo) {
            stepMotor(PUL2, Vel::AGUJAS_NORMAL);
        }
        int vel;
        if      (i < inicioConst) vel = map(i, 0,        inicioConst, Vel::RAMPA_INICIO, Vel::TRABAJO_MESA);
        else if (i < finConst)    vel = Vel::TRABAJO_MESA;
        else                      vel = map(i, finConst,  totalPasos,  Vel::TRABAJO_MESA, Vel::RAMPA_INICIO);
        stepMotor(PUL1, vel);
    }
    return true;
}

// ============================================================
//   PERFORAR
// ============================================================

bool perforar() {
    log("--- Perforando ---");
    if (agujasEnZonaSegura) {
        setDirAgujas(AGUJAS_BAJAR);
        agujasEnZonaSegura = false;
    }
    long timeoutBajada = (long)(MM_MAX_AGUJAS * PASOS_POR_MM_AGUJAS * 2);
    long contador = 0;
    while (agujasArriba()) {
        if (agujasAbajo()) {
            deshabilitarTodo();
            irAError("Limite inferior al salir del sensor superior");
            return false;
        }
        if (++contador > timeoutBajada) {
            irAError("Timeout bajando agujas");
            return false;
        }
        stepMotor(PUL2, Vel::AGUJAS_NORMAL);
    }
    log("Referencia superior pasada");
    float extra      = calcularExtraPerforacion(recetaActiva->french);
    long pasosExtra  = (long)(extra * PASOS_POR_MM_AGUJAS);
    for (long i = 0; i < pasosExtra; i++) {
        if (agujasAbajo()) {
            deshabilitarTodo();
            irAError("Limite inferior en bajada extra");
            return false;
        }
        stepMotor(PUL2, Vel::AGUJAS_NORMAL);
    }
    delay(50);
    setDirAgujas(AGUJAS_SUBIR);
    contador = 0;
    long timeoutSubida = (long)(MM_MAX_AGUJAS * PASOS_POR_MM_AGUJAS * 2);
    while (!agujasArriba()) {
        if (++contador > timeoutSubida) {
            agujasEnZonaSegura = false;
            irAError("Timeout subiendo agujas");
            return false;
        }
        stepMotor(PUL2, Vel::AGUJAS_NORMAL);
    }
    agujasEnZonaSegura = true;
    log("Perforacion completa");
    return true;
}

// ============================================================
//   PROCESAR SERIAL
// ============================================================

void procesarSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            bufferSerial.trim();
            bufferSerial.toUpperCase();
            if (bufferSerial.startsWith("PARTE")) {
                int num = bufferSerial.substring(5).toInt();
                if (num <= 0 || num > NUM_RECETAS) {
                    Serial.println("[ERROR] Parte invalida. Rango: 1-" + String(NUM_RECETAS));
                } else {
                    recetaActiva = recetas[num - 1];
                    Serial.println();
                    Serial.println("==========================================");
                    Serial.print  ("  Parte seleccionada : ");
                    Serial.println(recetaActiva->nombre);
                    Serial.print  ("  Agujeros           : ");
                    Serial.println(recetaActiva->totalAgujeros);
                    Serial.print  ("  French             : ");
                    Serial.println(recetaActiva->french);
                    Serial.println("==========================================");
                    Serial.println();
                }
            }
            if (bufferSerial == "START") {
                if (estadoActual == Estado::ESPERANDO) {
                    stopSolicitado = false;
                    agujeroActual  = 0;
                    estadoActual   = Estado::MOVIENDO_MESA;
                    Serial.println();
                    Serial.println("==========================================");
                    Serial.print  ("  Parte    : "); Serial.println(recetaActiva->nombre);
                    Serial.print  ("  Agujeros : "); Serial.println(recetaActiva->totalAgujeros);
                    Serial.println("==========================================");
                    Serial.println();
                } else if (estadoActual == Estado::ERROR) {
                    Serial.println("[WARN]  Hay un error activo. Envie RESET primero.");
                } else {
                    Serial.println("[WARN]  Sistema no esta listo para START.");
                }
            } else if (bufferSerial == "STOP") {
                stopSolicitado = true;
                log("STOP solicitado");
            } else if (bufferSerial == "RESET") {
                if (estadoActual == Estado::ERROR) {
                    stopSolicitado     = false;
                    agujeroActual      = 0;
                    agujasEnZonaSegura = false;
                    digitalWrite(ENA1, LOW);
                    digitalWrite(ENA2, LOW);
                    delay(500);
                    estadoActual = Estado::HOMING;
                    log("Reiniciando - ejecutando homing...");
                } else {
                    Serial.println("[WARN]  RESET solo aplica en estado ERROR.");
                }
            } else if (bufferSerial == "STATUS") {
                Serial.print("[INFO]  Estado   : ");
                switch (estadoActual) {
                    case Estado::HOMING:        Serial.println("HOMING");        break;
                    case Estado::ESPERANDO:     Serial.println("ESPERANDO");     break;
                    case Estado::MOVIENDO_MESA: Serial.println("MOVIENDO_MESA"); break;
                    case Estado::PERFORANDO:    Serial.println("PERFORANDO");    break;
                    case Estado::FIN:           Serial.println("FIN");           break;
                    case Estado::ERROR:         Serial.println("ERROR");         break;
                }
                Serial.print("[INFO]  Agujero  : ");
                Serial.print(agujeroActual);
                Serial.print("/");
                Serial.println(recetaActiva->totalAgujeros);
                Serial.print("[INFO]  Agujas   : ");
                Serial.println(agujasEnZonaSegura ? "ZONA SEGURA" : "FUERA");
            } else if (bufferSerial.length() > 0) {
                Serial.println("[WARN]  Comando no reconocido.");
                Serial.println("        Comandos: START | STOP | RESET | STATUS | PARTEn");
            }
            bufferSerial = "";
        } else {
            bufferSerial += c;
        }
    }
}

// ============================================================
//   SETUP
// ============================================================

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);
    pinMode(ENA1, OUTPUT); pinMode(DIR1, OUTPUT); pinMode(PUL1, OUTPUT);
    pinMode(ENA2, OUTPUT); pinMode(DIR2, OUTPUT); pinMode(PUL2, OUTPUT);
    pinMode(PIN_MESA_INICIO,   INPUT_PULLUP);
    pinMode(PIN_MESA_FINAL,    INPUT_PULLUP);
    pinMode(PIN_AGUJAS_ABAJO,  INPUT_PULLUP);
    pinMode(PIN_AGUJAS_ARRIBA, INPUT_PULLUP);
    digitalWrite(ENA1, LOW);
    digitalWrite(ENA2, LOW);
    delay(500);
    Serial.println();
    Serial.println("==========================================");
    Serial.println("   SISTEMA DE PONCHADO AUTOMATICO");
    Serial.println("   Proyecto de Graduacion | Teensy 4.1");
    Serial.print  ("   Recetas disponibles: ");
    Serial.println(NUM_RECETAS);
    Serial.println("==========================================");
    Serial.println();
}

// ============================================================
//   LOOP - MAQUINA DE ESTADOS
// ============================================================

void loop() {
    procesarSerial();
    if (estadoActual == Estado::ESPERANDO ||
        estadoActual == Estado::FIN) {
        verificarSeguridadGlobal();
    }
    switch (estadoActual) {
        case Estado::HOMING:
            if (!homingAgujas()) break;
            if (!homingMesa())   break;
            agujeroActual = 0;
            estadoActual  = Estado::ESPERANDO;
            Serial.println();
            Serial.print  ("Parte activa : "); Serial.println(recetaActiva->nombre);
            Serial.println("Envie START para iniciar.");
            Serial.println();
            break;

        case Estado::ESPERANDO:
            break;

        case Estado::MOVIENDO_MESA: {
            float distancia = (agujeroActual == 0)
                ? recetaActiva->offsetInicial
                : recetaActiva->espaciados[agujeroActual - 1];
            log(String("Agujero ") + (agujeroActual + 1) + "/" +
                recetaActiva->totalAgujeros + " -> " + String(distancia, 1) + " mm");
            if (!moverMesa(distancia)) break;
            estadoActual = Estado::PERFORANDO;
            break;
        }

        case Estado::PERFORANDO:
            if (!perforar()) break;
            agujeroActual++;
            estadoActual = (agujeroActual < recetaActiva->totalAgujeros)
                ? Estado::MOVIENDO_MESA
                : Estado::FIN;
            break;

        case Estado::FIN:
            Serial.println();
            Serial.println("==========================================");
            Serial.println("   PROCESO TERMINADO");
            Serial.print  ("   Agujeros realizados: ");
            Serial.println(recetaActiva->totalAgujeros);
            Serial.println("==========================================");
            Serial.println();
            log("Regresando a inicio...");
            if (!homingMesa()) break;
            agujeroActual = 0;
            estadoActual  = Estado::ESPERANDO;
            Serial.println("Listo para siguiente pieza. Envie START.");
            Serial.println();
            break;

        case Estado::ERROR:
            break;

        default:
            break;
    }
}
