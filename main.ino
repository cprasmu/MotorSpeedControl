/*
  Motor Speed Control with ESP32 and L298N H-Bridge
  Reads IR sensor for rotation count
  Serves a web page to control motor speed and direction
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Arduino.h> // For basic Arduino functions
// FreeRTOS is included by default in ESP32 Arduino core

// Web server on port 80
WebServer server(80);

// WiFi access point credentials
const char* ssid = "ESP32_Motor_Control";
const char* password = "12345678";

// Motor control pins (L298N)
const int motorIN1 = 27; // Direction pin 1
const int motorIN2 = 26; // Direction pin 2
const int motorPWM = 25; // Speed control (PWM)

// IR sensor pin
const int irSensorPin = 33;

// Variables for IR sensor counting
volatile long pulseCount = 0;
volatile long lastPulseCount = 0;
unsigned long lastTime = 0;
float rpm = 0.0;          // Output shaft RPM
float motorRpm = 0.0;     // Motor shaft RPM
long pulses = 0; // Declare pulses variable

// Motor state
String motorDirection = "Stop";
int motorSpeed = 0; // 0-255

// Encoder and gearbox constants
const int PULSES_PER_REVOLUTION = 11; // 11 slots on the encoder wheel
volatile int GEARBOX_RATIO = 16; // 16:1 reduction gearbox (changeable via web)

// Function to set motor direction and speed
void setMotor(String direction, int speed) {
  // Constrain speed between 0 and 255
  speed = constrain(speed, 0, 255);
  motorSpeed = speed;
  motorDirection = direction;

  if (direction == "Forward") {
    digitalWrite(motorIN1, HIGH);
    digitalWrite(motorIN2, LOW);
    analogWrite(motorPWM, speed);
  } else if (direction == "Backward") {
    digitalWrite(motorIN1, LOW);
    digitalWrite(motorIN2, HIGH);
    analogWrite(motorPWM, speed);
  } else { // Stop
    digitalWrite(motorIN1, LOW);
    digitalWrite(motorIN2, LOW);
    analogWrite(motorPWM, 0);
    motorSpeed = 0;
  }
}

// IR sensor interrupt handler
void IRAM_ATTR handleIRSensor() {
  pulseCount = pulseCount + 1;
}

// Function to rotate motor for a specific number of output shaft turns
void rotateMotorTurns(String direction, float turns) {
  // Calculate target pulses: turns * (pulses per motor revolution) * (gear ratio)
  long targetPulses = round(turns * PULSES_PER_REVOLUTION * GEARBOX_RATIO);
   
  // Reset pulse count by storing current value as baseline
  noInterrupts();
  long baselinePulseCount = pulseCount;
  interrupts();
   
  // Set motor direction
  setMotor(direction, 200); // Use reasonable speed (200/255)
   
  // Wait until target pulses reached (relative to baseline)
  while (true) {
    noInterrupts();
    long currentPulseCount = pulseCount;
    interrupts();
    
    long pulsesSinceBaseline = currentPulseCount - baselinePulseCount;
    
    if (pulsesSinceBaseline >= targetPulses) {
      break;
    }
    delay(10); // Small delay to prevent watchdog issues
  }
   
  // Stop motor
  setMotor("Stop", 0);
}

// Function to calculate RPM based on pulses per revolution
// Assuming the slotted wheel has N slots (we'll define PULSES_PER_REVOLUTION)
void calculateRPM() {
  unsigned long now = millis();
  if (now - lastTime >= 1000) { // Update every second
    // Disable interrupt temporarily to read pulseCount
    noInterrupts();
    long currentPulseCount = pulseCount;
    long pulses = currentPulseCount - lastPulseCount;
    lastPulseCount = currentPulseCount;
    interrupts();

    // Calculate motor shaft RPM: (pulses per second) * (60 seconds / pulses per revolution)
    float motorRPS = (pulses * 1.0) / ((now - lastTime) / 1000.0); // motor shaft revolutions per second
    motorRpm = motorRPS * 60.0; // motor shaft revolutions per minute
    
    // Calculate output shaft RPM after gearbox reduction
    rpm = motorRpm / GEARBOX_RATIO;
  
    lastTime = now;
  }
}

// HTML web page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 Motor Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }
    .button { padding: 10px 20px; font-size: 16px; margin: 10px; cursor: pointer; }
    .slider { width: 80%; }
    .speed-display { font-size: 24px; margin: 20px; }
    .rpm-display { font-size: 24px; margin: 20px; }
    .gear-display { font-size: 24px; margin: 20px; }
    .turn-button { background-color: #4CAF50; color: white; border: none; padding: 12px 24px; font-size: 18px; margin: 15px; cursor: pointer; border-radius: 4px; }
    .turn-button:hover { background-color: #45a049; }
    .turn-button:active { background-color: #3e8e41; }
    .ccw-button { background-color: #f44336; }
    .ccw-button:hover { background-color: #da190b; }
    .ccw-button:active { background-color: #b71c1c; }
    .gear-input { width: 80px; text-align: center; font-size: 18px; margin: 10px; padding: 5px; }
    .gear-button { background-color: #2196F3; color: white; border: none; padding: 8px 16px; font-size: 16px; margin: 10px; cursor: pointer; border-radius: 4px; }
    .gear-button:hover { background-color: #0b7dda; }
    .gear-button:active { background-color: #0a63bd; }
    .ten-turn-button { background-color: #FF9800; color: white; border: none; padding: 12px 24px; font-size: 18px; margin: 15px; cursor: pointer; border-radius: 4px; }
    .ten-turn-button:hover { background-color: #e68900; }
    .ten-turn-button:active { background-color: #cc7a00; }
    .ten-turn-ccw-button { background-color: #ff5722; }
    .ten-turn-ccw-button:hover { background-color: #e64a19; }
    .ten-turn-ccw-button:active { background-color: #cc4a00; }
  </style>
</head>
<body>
  <h1>ESP32 Motor Control</h1>
  <div>
    <button class="button" onclick="sendDirection('Forward')">Forward</button>
    <button class="button" onclick="sendDirection('Backward')">Backward</button>
    <button class="button" onclick="sendDirection('Stop')">Stop</button>
  </div>
  <div>
    <button class="turn-button" onclick="sendTurnCW()">Rotate Clockwise 1 Turn</button>
    <button class="turn-button ccw-button" onclick="sendTurnCCW()">Rotate Counterclockwise 1 Turn</button>
    <button class="ten-turn-button" onclick="sendTurn10CW()">Rotate Clockwise 10 Turns</button>
    <button class="ten-turn-button ten-turn-ccw-button" onclick="sendTurn10CCW()">Rotate Counterclockwise 10 Turns</button>
  </div>
  <div class="speed-display">Speed: <span id="speedValue">127</span></div>
  <input type="range" min="0" max="255" class="slider" id="speedSlider" value="127" onchange="updateSpeed(this.value)">
  <div class="gear-display">Gear Ratio: <span id="gearValue">16</span>:1</div>
  <input type="number" min="1" max="200" class="gear-input" id="gearInput" value="16">
  <button class="gear-button" onclick="updateGearRatio()">Set Gear Ratio</button>
  <div class="rpm-display">Output Shaft RPM: <span id="rpmValue">0</span></div>
  <div class="rpm-display">Motor Shaft RPM: <span id="motorRpmValue">0</span></div>
  <script>
    function sendDirection(dir) {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/setDirection?dir=" + dir, true);
      xhr.send();
    }
    function updateSpeed(speed) {
      document.getElementById("speedValue").innerText = speed;
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/setSpeed?speed=" + speed, true);
      xhr.send();
    }
    function sendTurnCW() {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/turnCW", true);
      xhr.send();
      // Show feedback
      this.innerText = "Rotating...";
      setTimeout(() => {
        this.innerText = "Rotate Clockwise 1 Turn";
      }, 3000);
    }
    function sendTurnCCW() {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/turnCCW", true);
      xhr.send();
      // Show feedback
      this.innerText = "Rotating...";
      setTimeout(() => {
        this.innerText = "Rotate Counterclockwise 1 Turn";
      }, 3000);
    }
    function sendTurn10CW() {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/turn10CW", true);
      xhr.send();
      // Show feedback
      this.innerText = "Rotating...";
      setTimeout(() => {
        this.innerText = "Rotate Clockwise 10 Turns";
      }, 8000); // Longer timeout for 10 turns
    }
    function sendTurn10CCW() {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/turn10CCW", true);
      xhr.send();
      // Show feedback
      this.innerText = "Rotating...";
      setTimeout(() => {
        this.innerText = "Rotate Counterclockwise 10 Turns";
      }, 8000); // Longer timeout for 10 turns
    }
    function updateGearRatio() {
      var gearInput = document.getElementById("gearInput");
      var gearValue = gearInput.value;
      document.getElementById("gearValue").innerText = gearValue;
      
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/setGearRatio?ratio=" + gearValue, true);
      xhr.send();
      
      // Show feedback
      this.innerText = "Setting...";
      setTimeout(() => {
        this.innerText = "Set Gear Ratio";
      }, 1000);
    }
    // Update RPM every second
    setInterval(function() {
      var xhr = new XMLHttpRequest();
      xhr.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
          document.getElementById("rpmValue").innerText = this.responseText;
        }
      };
      xhr.open("GET", "/getRPM", true);
      xhr.send();
    }, 1000);
    
    // Update motor RPM every second
    setInterval(function() {
      var xhr = new XMLHttpRequest();
      xhr.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
          document.getElementById("motorRpmValue").innerText = this.responseText;
        }
      };
      xhr.open("GET", "/getMotorRPM", true);
      xhr.send();
    }, 1000);
  </script>
</body>
</html>)rawliteral";

// Handle root URL
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

// Handle direction change
void handleDirection() {
  if (server.hasArg("dir")) {
    String dir = server.arg("dir");
    setMotor(dir, motorSpeed); // Keep current speed
    server.send(200, "text/plain", "Direction set to " + dir);
  } else {
    server.send(400, "text/plain", "Missing direction parameter");
  }
}

// Handle speed change
void handleSpeed() {
  if (server.hasArg("speed")) {
    int speed = server.arg("speed").toInt();
    setMotor(motorDirection, speed);
    server.send(200, "text/plain", "Speed set to " + String(speed));
  } else {
    server.send(400, "text/plain", "Missing speed parameter");
  }
}

// Handle RPM request
void handleRPM() {
  server.send(200, "text/plain", String((int)rpm));
}

// Handle motor RPM request
void handleMotorRPM() {
  server.send(200, "text/plain", String((int)motorRpm));
}

// Handle one turn clockwise request
void handleTurnCW() {
  // Run in a separate task to avoid blocking the web server
  xTaskCreatePinnedToCore(
    [](void* parameter) {
      rotateMotorTurns("Forward", 1.0); // One turn clockwise
      vTaskDelete(NULL);
    },
    "TurnCW",
    4096,
    NULL,
    1,
    NULL,
    0
  );
  server.send(200, "text/plain", "Rotating clockwise 1 turn");
}

// Handle gear ratio change
void handleGearRatio() {
  if (server.hasArg("ratio")) {
    int ratio = server.arg("ratio").toInt();
    // Constrain gear ratio to reasonable values
    ratio = constrain(ratio, 1, 200);
    GEARBOX_RATIO = ratio;
    server.send(200, "text/plain", "Gear ratio set to " + String(ratio) + ":1");
  } else {
    server.send(400, "text/plain", "Missing gear ratio parameter");
  }
}

// Handle one turn counterclockwise request
void handleTurnCCW() {
  // Run in a separate task to avoid blocking the web server
  xTaskCreatePinnedToCore(
    [](void* parameter) {
      rotateMotorTurns("Backward", 1.0); // One turn counterclockwise
      vTaskDelete(NULL);
    },
    "TurnCCW",
    4096,
    NULL,
    1,
    NULL,
    0
  );
  server.send(200, "text/plain", "Rotating counterclockwise 1 turn");
}

// Handle ten turns clockwise request
void handleTurn10CW() {
  // Run in a separate task to avoid blocking the web server
  xTaskCreatePinnedToCore(
    [](void* parameter) {
      rotateMotorTurns("Forward", 10.0); // Ten turns clockwise
      vTaskDelete(NULL);
    },
    "Turn10CW",
    4096,
    NULL,
    1,
    NULL,
    0
  );
  server.send(200, "text/plain", "Rotating clockwise 10 turns");
}

// Handle ten turns counterclockwise request
void handleTurn10CCW() {
  // Run in a separate task to avoid blocking the web server
  xTaskCreatePinnedToCore(
    [](void* parameter) {
      rotateMotorTurns("Backward", 10.0); // Ten turns counterclockwise
      vTaskDelete(NULL);
    },
    "Turn10CCW",
    4096,
    NULL,
    1,
    NULL,
    0
  );
  server.send(200, "text/plain", "Rotating counterclockwise 10 turns");
}

void setup() {
  Serial.begin(115200);
   
  // Set motor pins as outputs
  pinMode(motorIN1, OUTPUT);
  pinMode(motorIN2, OUTPUT);
  pinMode(motorPWM, OUTPUT);
   
  // Set IR sensor pin as input with interrupt
  pinMode(irSensorPin, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(irSensorPin), handleIRSensor, FALLING);
   
  // Initialize motor as stopped
  setMotor("Stop", 127); // Start at half speed
   
  // Start WiFi in access point mode
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
   
  // Define web server routes
  server.on("/", handleRoot);
  server.on("/setDirection", handleDirection);
  server.on("/setSpeed", handleSpeed);
  server.on("/getRPM", handleRPM);
  server.on("/getMotorRPM", handleMotorRPM);
  server.on("/turnCW", handleTurnCW);
  server.on("/turnCCW", handleTurnCCW);
  server.on("/turn10CW", handleTurn10CW);
  server.on("/turn10CCW", handleTurn10CCW);
  server.on("/setGearRatio", handleGearRatio);
   
  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  calculateRPM(); // Update RPM every second
}