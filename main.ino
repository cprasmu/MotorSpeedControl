/*
  Motor Speed Control with ESP32 and L298N H-Bridge
  Reads IR sensor for rotation count
  Serves a web page to control motor speed and direction
*/


#define USER_SETUP_INFO "User_Setup"
#define ST7789_DRIVER
#define TFT_WIDTH  135
#define TFT_HEIGHT 240
#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  23
#define TFT_BL   4
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_FONT8N
#define TOUCH_CS -1


#include <WiFi.h>
#include <WebServer.h>
#include <Arduino.h> // For basic Arduino functions
// FreeRTOS is included by default in ESP32 Arduino core
#include <ESP32Servo.h>
#include <TFT_eSPI.h> // Include TFT_eSPI library for TTGO T-display
#include <IRremote.hpp> // Notice the .hpp extension for newer versions

ESP32PWM pwm;
TFT_eSPI tft = TFT_eSPI(); // Create TFT_eSPI object


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
const int IR_RECEIVE_PIN = 32; // Digital Pin connected to OUT

// PWM settings
const int pwmFreq = 20000; // 5 kHz PWM frequency
const int pwmResolution = 8; // 8-bit resolution (0-255)

const int delayTime = 10; // Delay time in milliseconds for rotation loops

// Variables for IR sensor counting
volatile long pulseCount = 0;
volatile long lastPulseCount = 0;
unsigned long lastTime = 0;

float rpm = 0.0;          // Output shaft RPM
float motorRpm = 0.0;     // Motor shaft RPM
long pulses = 0; // Declare pulses variable

// Motor state
String motorDirection = "Stop";
int motorSpeed = 200 ; // Start at 200 (range 0-255)

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
  } else if (direction == "Backward") {
    digitalWrite(motorIN1, LOW);
    digitalWrite(motorIN2, HIGH);
  } else { // Stop
    digitalWrite(motorIN1, LOW);
    digitalWrite(motorIN2, LOW);
    speed = 0;
  }
  pwm.write(speed);

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
    delay(delayTime); // Small delay to prevent watchdog issues
  }
   
  // Stop motor
  setMotor("Stop", 0);
}

// Function to calculate RPM based on pulses per revolution
// Assuming the slotted wheel has N slots (we'll define PULSES_PER_REVOLUTION)
void calculateRPM() {
  unsigned long now = millis();
  if (now - lastTime >= 250) { // Update every 250ms
    // Disable interrupt temporarily to read pulseCount
    noInterrupts();
    long currentPulseCount = pulseCount;
    long pulses = currentPulseCount - lastPulseCount;
    lastPulseCount = currentPulseCount;
    interrupts();

    // Calculate time interval in seconds
    float timeInSeconds = (now - lastTime) / 1000.0;
    
    // Calculate motor shaft RPM: (pulses * 60) / (pulses per revolution * time in seconds)
    // Pulses per revolution = PULSES_PER_REVOLUTION (11 slots on encoder wheel)
    motorRpm = (pulses * 60.0) / (PULSES_PER_REVOLUTION * timeInSeconds);
    
    // Calculate output shaft RPM after gearbox reduction
    rpm = motorRpm / GEARBOX_RATIO;
  
    lastTime = now;
  }
}

void updateDisplay() {

  // Display motor speed
  tft.setCursor(0, 0);
  tft.print("Speed: ");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print(motorSpeed);
  tft.print("      ");

  // Display gear ratio
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 25);
  tft.print("Gear: ");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print(GEARBOX_RATIO);
  tft.print(":1");
  tft.print("      ");

  // Display output shaft RPM
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 50);
  tft.print("Out RPM: ");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print((int)rpm);
  tft.print("      ");

  // Display motor shaft RPM
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 75);
  tft.print("Mot RPM: ");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print((int)motorRpm);
  tft.print("      ");
  
  // Display motor direction
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 100);
  tft.print("Dir: ");
 
  if (motorDirection == "Forward") {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print("Forward        ");
  } else if (motorDirection == "Backward") {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print("Backward       ");
  } else {
     tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.print("Stop              ");
  }
}
// HTML web page
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 Motor Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      font-family: Arial, sans-serif; 
      text-align: center; 
      margin-top: 50px;
      background-color: #121212;
      color: #ffffff;
    }
    .button { 
      padding: 10px 20px; 
      font-size: 16px; 
      margin: 10px; 
      cursor: pointer; 
      background-color: #2a2a2a;
      color: #cccccc;
      border: 1px solid #444444;
      border-radius: 4px;
      width: 100px;
      text-align: center;
    }
    .button:hover {
      background-color: #333333;
    }
    .button:active {
      background-color: #1f1f1f;
    }
    .stop-button {
      background-color: #ff0000;
      border-color: #cc0000;
      color: #ffffff;
    }
    .stop-button:hover {
      background-color: #ff3333;
    }
    .stop-button:active {
      background-color: #cc0000;
    }
    .slider { 
      width: 80%; 
      background: #1f1f1f;
    }
    .speed-display, .rpm-display, .gear-display { 
      font-size: 24px; 
      margin: 20px;
      color: #00ff88;
    }
    .rotation-button {
      background-color: #2a2a2a;
      color: #cccccc;
      border: 1px solid #444444;
      padding: 12px 24px;
      font-size: 18px;
      margin: 10px;
      cursor: pointer;
      border-radius: 4px;
      min-width: 80px;
    }
    .rotation-button:hover {
      background-color: #333333;
    }
    .rotation-button:active {
      background-color: #1f1f1f;
    }
    .gear-input { 
      width: 80px; 
      text-align: center; 
      font-size: 18px; 
      margin: 10px; 
      padding: 5px;
      background-color: #2a2a2a;
      color: #cccccc;
      border: 1px solid #444444;
      border-radius: 4px;
    }
    .gear-button {
      background-color: #2a2a2a;
      color: #cccccc;
      border: 1px solid #444444;
      padding: 8px 16px;
      font-size: 16px;
      margin: 10px;
      cursor: pointer;
      border-radius: 4px;
    }
    .speed-input { 
      width: 80px; 
      text-align: center; 
      font-size: 18px; 
      margin: 10px; 
      padding: 5px;
      background-color: #2a2a2a;
      color: #cccccc;
      border: 1px solid #444444;
      border-radius: 4px;
    }
    .speed-button {
      background-color: #2a2a2a;
      color: #cccccc;
      border: 1px solid #444444;
      padding: 8px 16px;
      font-size: 16px;
      margin: 10px;
      cursor: pointer;
      border-radius: 4px;
    }
    .gear-button:hover {
      background-color: #333333;
    }
    .gear-button:active {
      background-color: #1f1f1f;
    }
    .button-container {
      margin: 20px 0;
    }
    .button-row {
      display: flex;
      justify-content: center;
      gap: 15px;
      margin: 10px 0;
    }
  </style>
</head>
<body>
  <h1>ESP32 Motor Control</h1>
  <div>
    <button class="button" onclick="sendDirection('Forward')">Forward</button>
    <button class="button" onclick="sendDirection('Backward')">Backward</button>
  </div>
  <div class="button-container">
    <div class="button-row">
      <button class="rotation-button" onclick="sendTurnCCW()">-1</button>
      <button class="rotation-button" onclick="sendTurnCW()">+1</button>
    </div>
    <div class="button-row">
      <button class="rotation-button" onclick="sendTurn10CCW()">-10</button>
      <button class="rotation-button" onclick="sendTurn10CW()">+10</button>
    </div>
    <div class="button-row">
      <button class="button stop-button" onclick="sendDirection('Stop')">Stop</button>
    </div>
  </div>

  <input type="number" min="1" max="225" class="speed-input" id="speedInput" value="200">
  <button class="speed-button" onclick="updateSpeed()">Set Speed</button>
  <input type="number" min="1" max="200" class="gear-input" id="gearInput" value="16">
  <button class="gear-button" onclick="updateGearRatio()">Set Gear Ratio</button>

  <div class="speed-display">Speed: <span id="speedValue">200</span></div>
  <div class="gear-display">Gear Ratio: <span id="gearValue">16</span>:1</div>
  <div class="rpm-display">Output Shaft RPM: <span id="rpmValue">0</span></div>
  <div class="rpm-display">Motor Shaft RPM: <span id="motorRpmValue">0</span></div>

  <script>
    function sendDirection(dir) {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/setDirection?dir=" + dir, true);
      xhr.send();
    }

    function updateSpeed() {

      speed = document.getElementById("speedInput").value;
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
        this.innerText = "+1";
      }, 3000);
    }
    function sendTurnCCW() {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/turnCCW", true);
      xhr.send();
      // Show feedback
      this.innerText = "Rotating...";
      setTimeout(() => {
        this.innerText = "-1";
      }, 3000);
    }
    function sendTurn10CW() {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/turn10CW", true);
      xhr.send();
      // Show feedback
      this.innerText = "Rotating...";
      setTimeout(() => {
        this.innerText = "+10";
      }, 8000); // Longer timeout for 10 turns
    }
    function sendTurn10CCW() {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/turn10CCW", true);
      xhr.send();
      // Show feedback
      this.innerText = "Rotating...";
      setTimeout(() => {
        this.innerText = "-10";
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
    // Update RPM every 250ms
    setInterval(function() {
      var xhr = new XMLHttpRequest();
      xhr.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
          document.getElementById("rpmValue").innerText = this.responseText;
        }
      };
      xhr.open("GET", "/getRPM", true);
      xhr.send();
    }, 250);
    
    setInterval(function() {
      var xhr = new XMLHttpRequest();
      xhr.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
          document.getElementById("speedValue").innerText = this.responseText;
        }
      };
      xhr.open("GET", "/getSpeed", true);
      xhr.send();
    }, 250);

    // Update motor RPM every 250ms
    setInterval(function() {
      var xhr = new XMLHttpRequest();
      xhr.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
          document.getElementById("motorRpmValue").innerText = this.responseText;
        }
      };
      xhr.open("GET", "/getMotorRPM", true);
      xhr.send();
    }, 250);
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
void handleSetSpeed() {
  if (server.hasArg("speed")) {
    int speed = server.arg("speed").toInt();
    setMotor(motorDirection, speed);
    server.send(200, "text/plain", "Speed set to " + String(speed));
  } else {
    server.send(400, "text/plain", "Missing speed parameter");
  }
}

// Handle speed request
void handleGetSpeed() {
  server.send(200, "text/plain", String((int)motorSpeed));
}

// Handle RPM request
void handleRPM() {
  server.send(200, "text/plain", String((int)rpm));
}

// Handle motor RPM request
void handleMotorRPM() {
  server.send(200, "text/plain", String((int)motorRpm));
}

void handleGetStatus() {
  server.send(200, "text/plain", "{'speed':" + String((int)motorSpeed) + ",'gearboxRatio':" + GEARBOX_RATIO + ",'rpm':" +  String((int)rpm) + "'motorRpm':" + String((int)motorRpm) + "'motorDirection':'" + motorDirection + "'}");
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
void handleIr() {

 if (IrReceiver.decode()) {

  if (IrReceiver.decodedIRData.protocol == APPLE) {
      switch (IrReceiver.decodedIRData.command) {
        case 8: 
          setMotor("Forward", motorSpeed); 
          break;

        case 7:
          setMotor("Backward", motorSpeed); 
          break;

        case 93:
          setMotor("Stop", motorSpeed); 
          break;

        case 11:
          if (motorSpeed < 255) {
            motorSpeed += 1;
          }
          setMotor(motorDirection, motorSpeed); 
          break;

        case 13:
          if (motorSpeed > 0) {
            motorSpeed -= 1;
          }
          setMotor(motorDirection, motorSpeed); 
          break;

        case 94:
          handleTurn10CW();
          break;
  
        case 2:
          handleTurn10CCW();
          break;
      }

    }
    
  }
    // Resume listening for the next signal
    IrReceiver.resume(); 

}
void setup() {
  ESP32PWM::allocateTimer(0);
	ESP32PWM::allocateTimer(1);
	ESP32PWM::allocateTimer(2);
	ESP32PWM::allocateTimer(3);

  pwm.attachPin(motorPWM, pwmFreq, pwmResolution); // 1KHz 10 bits
  Serial.begin(115200);
   
  // Set motor pins as outputs
  pinMode(motorIN1, OUTPUT);
  pinMode(motorIN2, OUTPUT);
   
  pinMode(TFT_BL, OUTPUT); 
  digitalWrite(TFT_BL, HIGH); // Turn on backlight 

   // Initialize TFT display
  tft.init();
  tft.setRotation(1); // Adjust rotation as needed (0-3)
  tft.fillScreen(TFT_BLACK);
  
  // Set text color to white for labels, cyan for values (dark mode theme)
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(4); // Small font for labels
  updateDisplay();

  // Set IR sensor pin as input with interrupt
  pinMode(irSensorPin, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(irSensorPin), handleIRSensor, FALLING);
   
  // Initialize motor 
  setMotor("Stop", motorSpeed);
   
  // Start WiFi in access point mode
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
   
  // Define web server routes
  server.on("/", handleRoot);
  server.on("/setDirection", handleDirection);
  server.on("/setSpeed", handleSetSpeed);
  server.on("/getSpeed", handleGetSpeed);
  server.on("/getRPM", handleRPM);
  server.on("/getMotorRPM", handleMotorRPM);
  server.on("/turnCW", handleTurnCW);
  server.on("/turnCCW", handleTurnCCW);
  server.on("/turn10CW", handleTurn10CW);
  server.on("/turn10CCW", handleTurn10CCW);
  server.on("/setGearRatio", handleGearRatio);
  server.on("/getStatus", handleGetStatus);

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK); 

  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  handleIr();
  calculateRPM(); // Update RPM every 250ms
  updateDisplay(); // Update TFT display

}