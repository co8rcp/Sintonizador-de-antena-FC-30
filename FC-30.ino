/*************************************************************************
 * FC-30 Tuner Emulator Full Band Version for FT-857D
 * Cortesía de CO8RCP  Ray Manuel Castellanos Parra co8rcp @gmail.com
 * Bandas cubiertas (igual que el FC-30 original):
 * - 160m (1.8-2.0 MHz)
 * - 80m (3.5-4.0 MHz)
 * - 60m (5.3-5.4 MHz)*
 * - 40m (7.0-7.3 MHz)
 * - 30m (10.1-10.15 MHz)
 * - 20m (14.0-14.35 MHz)
 * - 17m (18.068-18.168 MHz)
 * - 15m (21.0-21.45 MHz)
 * - 12m (24.89-24.99 MHz)
 * - 10m (28.0-29.7 MHz)
 * - 6m (50.0-54.0 MHz)*
 * 
 * (*) Nota: El FC-30 original no soporta 60m y 6m oficialmente,
 * pero esta versión los incluye para cobertura completa
 * 
 **************************************************************************/

#include <SoftwareSerial.h>

// Pines de comunicación con la radio
#define RADIO_RX_PIN 10
#define RADIO_TX_PIN 11
SoftwareSerial radioSerial(RADIO_RX_PIN, RADIO_TX_PIN);

// Pines de control para relés
const int inductancePins[4] = {2, 3, 4, 5};    // ABCD inductancias
const int inputCapPins[4] = {6, 7, 8, 9};      // ABCD capac. entrada
const int outputCapPins[4] = {A0, A1, A2, A3}; // ABCD capac. salida

// Pines adicionales
#define TX_RELAY_PIN 12     // Relay de TX dentro del tuner
#define TUNE_STATUS_PIN 13  // LED/buzzer de estado de sintonía
#define TUNE_BUTTON_PIN A6  // Botón manual de sintonía (opcional)

// Parámetros del tuner
#define TUNING_DELAY 3000   // 3 segundos para sintonía
#define MEMORY_SLOTS 11     // Número de bandas memorizadas (160m-6m)

// Estados del tuner
enum TunerState {
  TUNER_IDLE,
  TUNER_TUNING,
  TUNER_TUNED,
  TUNER_ERROR,
  TUNER_BYPASS
};

// Estructura para memoria de banda
struct BandMemory {
  unsigned long minFreq;
  unsigned long maxFreq;
  byte inductance;
  byte inputCap;
  byte outputCap;
  String bandName;
};

// Variables de estado
TunerState tunerState = TUNER_IDLE;
unsigned long tuningStartTime = 0;
unsigned long currentFreq = 7100000; // Frecuencia inicial 40m
bool tunerEnabled = true;
bool txStatus = false;
bool lastTuneButtonState = HIGH;

// Memoria de bandas (frecuencias en Hz) - Valores iniciales típicos
BandMemory memories[MEMORY_SLOTS] = {
  {1800000, 2000000, 5, 3, 7, "160m"},    // 160m
  {3500000, 4000000, 3, 5, 2, "80m"},     // 80m
  {5300000, 5400000, 4, 4, 3, "60m"},     // 60m (no oficial en FC-30)
  {7000000, 7300000, 2, 4, 5, "40m"},     // 40m
  {10100000, 10150000, 1, 3, 4, "30m"},   // 30m
  {14000000, 14350000, 1, 2, 3, "20m"},   // 20m
  {18068000, 18168000, 0, 1, 2, "17m"},   // 17m
  {21000000, 21450000, 0, 1, 1, "15m"},   // 15m
  {24890000, 24990000, 0, 0, 1, "12m"},   // 12m
  {28000000, 29700000, 0, 0, 0, "10m"},   // 10m
  {50000000, 54000000, 0, 0, 0, "6m"}     // 6m (no oficial en FC-30)
};

void setup() {
  // Inicializar comunicación con la radio
  radioSerial.begin(9600);
  
  // Configurar pines de control de relés
  for (int i = 0; i < 4; i++) {
    pinMode(inductancePins[i], OUTPUT);
    pinMode(inputCapPins[i], OUTPUT);
    pinMode(outputCapPins[i], OUTPUT);
  }
  
  // Configurar pines adicionales
  pinMode(TX_RELAY_PIN, OUTPUT);
  pinMode(TUNE_STATUS_PIN, OUTPUT);
  pinMode(TUNE_BUTTON_PIN, INPUT_PULLUP); // Botón opcional
  
  // Inicializar relés
  resetRelays();
  
  // Ajustar inicialmente para la banda actual
  adjustForCurrentBand();
  
  // Inicializar serial para depuración
  Serial.begin(115200);
  Serial.println("FC-30 Full Band Tuner Emulator - Ready");
}

void loop() {
  // Manejar comandos de la radio
  if (radioSerial.available()) {
    String command = radioSerial.readStringUntil('\n');
    command.trim();
    processCommand(command);
  }
  
  // Manejar el proceso de sintonía
  if (tunerState == TUNER_TUNING) {
    if (millis() - tuningStartTime >= TUNING_DELAY) {
      completeTuning();
    } else {
      // Durante la sintonía podríamos hacer barridos (simulados aquí)
      simulateTuningSweep();
    }
  }
  
  // Verificar botón manual de sintonía (opcional)
  checkTuneButton();
  
  // Actualizar estado del relay de TX
  digitalWrite(TX_RELAY_PIN, txStatus ? HIGH : LOW);
}

// Función para procesar comandos CAT
void processCommand(String command) {
  if (command == "TUNE") {
    startTuning();
  } 
  else if (command == "TUNER?") {
    sendTunerStatus();
  }
  else if (command == "TUNER RESET") {
    resetTuner();
  }
  else if (command == "TUNER ON") {
    enableTuner(true);
  }
  else if (command == "TUNER OFF") {
    enableTuner(false);
  }
  else if (command == "TUNER MEM?") {
    sendMemoryStatus();
  }
  else if (command == "TX ON") {
    txStatus = true;
    radioSerial.println("TX ON");
  }
  else if (command == "TX OFF") {
    txStatus = false;
    radioSerial.println("TX OFF");
  }
  else if (command.startsWith("FREQ ")) {
    currentFreq = command.substring(5).toInt();
    checkBandChange();
    radioSerial.println("FREQ OK");
  }
  else {
    radioSerial.println("TUNER ERROR");
    Serial.print("Unknown command: ");
    Serial.println(command);
  }
}

// Ajustar relés para la banda actual
void adjustForCurrentBand() {
  for (int i = 0; i < MEMORY_SLOTS; i++) {
    if (currentFreq >= memories[i].minFreq && currentFreq <= memories[i].maxFreq) {
      setRelays(memories[i].inductance, memories[i].inputCap, memories[i].outputCap);
      Serial.print("Ajustado para banda ");
      Serial.println(memories[i].bandName);
      return;
    }
  }
  
  // Si no está en ninguna banda conocida, usar valores por defecto
  setRelays(0, 0, 0);
  Serial.println("Frecuencia fuera de bandas - Valores por defecto");
}

// Verificar si ha cambiado de banda
void checkBandChange() {
  static unsigned long lastBand = 0;
  bool bandChanged = false;
  
  // Determinar banda actual
  byte currentBand = 255; // Inválido
  for (byte i = 0; i < MEMORY_SLOTS; i++) {
    if (currentFreq >= memories[i].minFreq && currentFreq <= memories[i].maxFreq) {
      currentBand = i;
      break;
    }
  }
  
  // Comparar con última banda
  if (currentBand != lastBand) {
    bandChanged = true;
    lastBand = currentBand;
  }
  
  if (bandChanged) {
    Serial.print("Cambio de banda detectado: ");
    if (currentBand < MEMORY_SLOTS) {
      Serial.println(memories[currentBand].bandName);
    } else {
      Serial.println("Fuera de banda");
    }
    
    // Reajustar solo si no estamos en medio de una sintonía
    if (tunerState != TUNER_TUNING) {
      adjustForCurrentBand();
    }
  }
}

// Iniciar proceso de sintonía
void startTuning() {
  if (!tunerEnabled) {
    radioSerial.println("TUNER BYPASS");
    return;
  }
  
  digitalWrite(TUNE_STATUS_PIN, HIGH);
  tunerState = TUNER_TUNING;
  tuningStartTime = millis();
  radioSerial.println("TUNER TUNING");
  
  Serial.println("Iniciando proceso de sintonía...");
}

// Simular barrido durante sintonía
void simulateTuningSweep() {
  static unsigned long lastSweep = 0;
  if (millis() - lastSweep > 300) { // Cada 300ms
    lastSweep = millis();
    
    // Barrido simulado - en una implementación real usarías mediciones de SWR
    byte inductance = random(0, 8);
    byte inputCap = random(0, 8);
    byte outputCap = random(0, 8);
    setRelays(inductance, inputCap, outputCap);
    
    Serial.print("Barrido: L=");
    Serial.print(inductance);
    Serial.print(", Ci=");
    Serial.print(inputCap);
    Serial.print(", Co=");
    Serial.println(outputCap);
  }
}

// Completar proceso de sintonía
void completeTuning() {
  tunerState = TUNER_TUNED;
  digitalWrite(TUNE_STATUS_PIN, LOW);
  
  // Guardar valores "óptimos" encontrados (simulado)
  byte foundInductance = random(0, 8);
  byte foundInputCap = random(0, 8);
  byte foundOutputCap = random(0, 8);
  
  setRelays(foundInductance, foundInputCap, foundOutputCap);
  
  // Actualizar memoria para esta banda
  for (int i = 0; i < MEMORY_SLOTS; i++) {
    if (currentFreq >= memories[i].minFreq && currentFreq <= memories[i].maxFreq) {
      memories[i].inductance = foundInductance;
      memories[i].inputCap = foundInputCap;
      memories[i].outputCap = foundOutputCap;
      
      Serial.print("Banda ");
      Serial.print(memories[i].bandName);
      Serial.println(" optimizada y guardada");
      break;
    }
  }
  
  radioSerial.println("TUNER OK");
  Serial.println("Sintonía completada");
}

// Configurar todos los relés
void setRelays(byte inductanceVal, byte inputCapVal, byte outputCapVal) {
  // Controlar relés de inductancias (convertir valor decimal a ABCD)
  for (int i = 0; i < 4; i++) {
    digitalWrite(inductancePins[i], (inductanceVal >> i) & 0x01);
  }
  
  // Controlar relés de capacitancias de entrada
  for (int i = 0; i < 4; i++) {
    digitalWrite(inputCapPins[i], (inputCapVal >> i) & 0x01);
  }
  
  // Controlar relés de capacitancias de salida
  for (int i = 0; i < 4; i++) {
    digitalWrite(outputCapPins[i], (outputCapVal >> i) & 0x01);
  }
  
  Serial.print("Relés configurados - L:");
  Serial.print(inductanceVal);
  Serial.print(", Ci:");
  Serial.print(inputCapVal);
  Serial.print(", Co:");
  Serial.println(outputCapVal);
}

// Resetear todos los relés
void resetRelays() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(inductancePins[i], LOW);
    digitalWrite(inputCapPins[i], LOW);
    digitalWrite(outputCapPins[i], LOW);
  }
  digitalWrite(TX_RELAY_PIN, LOW);
  digitalWrite(TUNE_STATUS_PIN, LOW);
}

// Verificar botón manual de sintonía
void checkTuneButton() {
  bool currentState = digitalRead(TUNE_BUTTON_PIN);
  if (currentState != lastTuneButtonState) {
    delay(50); // Debounce
    if (digitalRead(TUNE_BUTTON_PIN)) {
      if (lastTuneButtonState == LOW) {
        startTuning();
      }
    }
    lastTuneButtonState = currentState;
  }
}

// Enviar estado del tuner
void sendTunerStatus() {
  switch (tunerState) {
    case TUNER_IDLE: radioSerial.println("TUNER IDLE"); break;
    case TUNER_TUNING: radioSerial.println("TUNER TUNING"); break;
    case TUNER_TUNED: radioSerial.println("TUNER OK"); break;
    case TUNER_ERROR: radioSerial.println("TUNER ERROR"); break;
    case TUNER_BYPASS: radioSerial.println("TUNER BYPASS"); break;
  }
}

// Resetear el tuner
void resetTuner() {
  tunerState = TUNER_IDLE;
  resetRelays();
  adjustForCurrentBand();
  radioSerial.println("TUNER READY");
  Serial.println("Tuner reiniciado");
}

// Habilitar/deshabilitar el tuner
void enableTuner(bool enable) {
  tunerEnabled = enable;
  tunerState = enable ? TUNER_IDLE : TUNER_BYPASS;
  radioSerial.println(enable ? "TUNER ON" : "TUNER OFF");
  Serial.print("Tuner ");
  Serial.println(enable ? "habilitado" : "deshabilitado");
}

// Enviar estado de memorias
void sendMemoryStatus() {
  String status = "MEM ";
  for (int i = 0; i < MEMORY_SLOTS; i++) {
    status += (currentFreq >= memories[i].minFreq && currentFreq <= memories[i].maxFreq) ? "1" : "0";
  }
  radioSerial.println(status);
}
