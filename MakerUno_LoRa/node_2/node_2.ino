#include <dht11.h>
#include <SPI.h>
#include <RHMesh.h>
#include <RH_RF95.h>
#include <SoftwareSerial.h>

/* Definitions for easy use to setup node ID. */
#define NODE_1 1
#define NODE_2 2
#define NODE_3 3
#define NODE_4 4
#define NODE_5 5

/* Definitions for LoRa x Arduino */
#define RFM95_CS 10
#define RFM95_RST 7
#define RFM95_INT 2
#define RF95_FREQ 923.0  // IMDA Singapore regulations -> 920 to 925 MHz

/* Definitions of constraints to prevent memory leak */
#define MAX_NODES 5
#define MAX_PAYLOAD_SIZE 50
#define SENSOR_DATA_SIZE 5
#define MAX_PAYLOAD_SIZE_SENSOR_DATA (MAX_NODES * SENSOR_DATA_SIZE)

/* Definition of retry/timing related information */
#define RETRY_COUNT 3
#define TIMEOUT 3000
#define NO_COMMUNICATION_TIMEOUT 180000
#define BROADCAST_INTERVAL 15000
#define RETRY_DELAY 100

/* Definitions of Sensor Related Information */
#define DHT11_PIN 8
#define SMOKE_SENSOR_PIN A0
#define load_Res 10
#define air_factor 9.83
#define IR_SENSOR_PIN 3
#define TEMPERATURE_THRESHOLD 45  // 50°C temperature
#define SMOKE_THRESHOLD 100       // 100 ppm smoke
#define HUMIDITY_THRESHOLD 90     // 90% humidity

// Change accordingly for different nodes. Our team did a dynamic version but takes up alot memory, hence removed.
const uint8_t nodeID = NODE_2;

/* History of existing nodes available in our network, to be used as route destination & discovery in mesh. */
uint8_t existingNodesArray[MAX_NODES] = {};  // Start with an empty array
uint8_t existingNodesCount = 0;              // Track the number of existing nodes

// States & Observers to control token circulation within network.
uint8_t tokenHolderIndex;     // Mutable variable which identifies the next token holder
bool tokenPosession;          // Flag to indiate the device is a token holder(Transmit Mode) or not (Recieve Mode)
bool tokenInNetwork;          // Monitor if there is an existing token on network
uint8_t pendingJoinNode = 0;  // Start with an empty array
bool outdatedRouteFlag = 1;   // Flag used to update routes when there is a new node that joined

// Message struct to send messages/recieve messages across our devices/network
struct Message {
  uint8_t type;                                      // Allowable message type (1-5)
  char payload[MAX_PAYLOAD_SIZE - sizeof(uint8_t)];  // Payload size which has account for type's size
};

/* Time tracker to be used for monitoring of any failures in our network e.g. tokenHolder has failed. */
unsigned long previousNeighbourBroadcast = 0;
unsigned long lastHeardTime = 0;
uint8_t deviceDownCounter;

/* Initializations of Sensor Related Information */
dht11 DHT11;                                 // Temperature & Humidity Sensor
float SmokeCurve[3] = { 2.3, 0.53, -0.44 };  // Smoke Sensor - (x,y,z) slope
float Res = 0;                               // Smoke Sensor - Resistance for calibration
uint8_t paxCounter = 0;                      // IR sensor - To count & monitor pax data

/* LoRa - Radiohead Mesh Library  */
RH_RF95 driver(RFM95_CS, RFM95_INT);
RHMesh manager(driver, nodeID);

/* For Uno to NodeMCU communication */
SoftwareSerial ArduinoUno(6, 5);  // NodeMCU Pin : MakerUno Pin- > D2:5 && D3:6

/* sensorData Struct which will be used to create a global table which allows storage of all node's sensor data */
struct sensorData {
  uint8_t nodeID;        // Node ID
  uint8_t tempData;      // Temperature data (DHT11 Sensor)
  uint8_t paxData;       // Pax data (IR Sensor)
  uint8_t smokeData;     // Smoke sensor data (MQ Sensor)
  uint8_t humidityData;  // Humidity sensor data (DHT11 Sensor)
};

sensorData sensorTable[MAX_NODES];  // 5 (row) x 5 (column) table which contains all the sensor data of all nodes.

void setup() {
  // LoRa Manual Reset (Part 1/2).
  pinMode(RFM95_RST, OUTPUT);
  digitalWrite(RFM95_RST, HIGH);

  Serial.begin(9600);
  ArduinoUno.begin(4800);

  // LoRa Manual Reset (Part 2/2).
  digitalWrite(RFM95_RST, LOW);
  delay(10);
  digitalWrite(RFM95_RST, HIGH);
  delay(2000);

  Serial.print(F("Device Node ID: "));
  Serial.println(nodeID, "\n");

  Serial.println(F("Mesh Starting..."));
  /* Initialize our LoRa mesh manager (For RHMesh Library) */
  if (!manager.init()) {
    Serial.println(F("Mesh fail"));
    while (1)
      ;
  }
  Serial.println(F("Mesh OK"));

  /* Configure LoRa to frequency */
  if (!driver.setFrequency(RF95_FREQ)) {
    Serial.println(F("Frequency failed"));
    while (1)
      ;
  }
  Serial.print(F("Freq: "));
  Serial.println(RF95_FREQ);
  manager.setRetries(5);  // Configure Retries to 5

  /* Set all the flags to 0 which indicate that our device is new */
  deviceDownCounter = 0;
  tokenInNetwork = 0;
  tokenPosession = 0;
}

void loop() {

  /* This transmit a broadcast at a 15s interval to find neighbours. Applicable only if it is a new node that joined network/restarted node. */
  if (!tokenInNetwork && millis() - previousNeighbourBroadcast > BROADCAST_INTERVAL) {
    Message neighbourDiscoveryMsg;
    neighbourDiscoveryMsg.type = 1;  // Type 1 - Indicates broadcast message to establish neighbour
    strcpy(neighbourDiscoveryMsg.payload, "Im New!");

    if (manager.sendtoWait((uint8_t *)&neighbourDiscoveryMsg, sizeof(neighbourDiscoveryMsg), RH_BROADCAST_ADDRESS) == RH_ROUTER_ERROR_NONE) {
      Serial.println(F("Neighbour Discovery Message Sent"));
    } else {
      Serial.println(F("Neighbour Broadcast has failed. Trying again in 30 seconds"));
    }
    previousNeighbourBroadcast = millis();
  }

  /* Self recovery system which allows a non-token holder to step up and assume the role of a token holder in the event of a failure of the token holder node. */
  if (tokenInNetwork && millis() - lastHeardTime > NO_COMMUNICATION_TIMEOUT) {
    // If entered this portion, it means this node has been stuck in listening for a very long time. Take over the role of TX
    Serial.println(F("Communication Timeout. Will Take Over."));
    establishRouteToNeighbours();
    lastHeardTime = millis();
    tokenPosession = 1;  // Device will take over the token.
  }

  /* TX MODE: tokenPosession is held by this node */
  while (tokenPosession) {
    Serial.println(F("=YOU HAVE TOKEN="));
    delay(500);

    // Check if any pendingNodes, if any, add to array & reliably inform all other node except itself. & clear the pending array.
    // Recievers of this add to array will then reset their pendingNodes value.
    if (pendingJoinNode != 0) {
      Serial.println(pendingJoinNode);
      outdatedRouteFlag = 1;
      existingNodesArray[existingNodesCount++] = pendingJoinNode;  // Add the new node into our existing node list.
      printExistingNodes();

      Message addIntoNetworkMsg;
      addIntoNetworkMsg.type = 2;  // Reply with message type '2' to indicate a reply containing existing node list.
      strcpy(addIntoNetworkMsg.payload, serializeExistingNodes().c_str());
      Serial.println(addIntoNetworkMsg.payload);

      for (int i = 0; i < existingNodesCount; i++) {
        if (existingNodesArray[i] != nodeID) {
          if (manager.sendtoWait((uint8_t *)&addIntoNetworkMsg, sizeof(addIntoNetworkMsg), existingNodesArray[i]) == RH_ROUTER_ERROR_NONE) {
            Serial.print(F("Informed updated array to node: "));
            Serial.println(existingNodesArray[i]);
          } else {
            Serial.print(F("Informing of updated Array Failed to node: "));
            Serial.println(existingNodesArray[i]);
          }
          delay(50);
        }
      }

      pendingJoinNode = 0;  // Reset Pending Join Nodes
      Serial.println(F("Done Broadcasting changes of existingNodeArray"));
      delay(100);
    }

    if (outdatedRouteFlag == 1) {
      establishRouteToNeighbours();
      delay(100);
    }

    pollSensors();
    update_broadcast_sensor_table();

    // This delay portion is unnessary but it is used to simulate our device being a token holder for longer duration for demostration purpose.
    Serial.println(F("===Transmitting==="));
    delay(300);
    Serial.println(F("===Transmitting==="));
    delay(300);
    Serial.println(F("===Transmitting==="));
    delay(300);
    Serial.println(F("===Transmitting==="));
    delay(300);
    Serial.println(F("===Transmitting==="));
    delay(300);
    Serial.println(F("===Transmitting==="));
    delay(300);
    Serial.println(F("===Transmitting==="));
    delay(300);
    Serial.println(F("===Transmitting==="));
    delay(300);
    Serial.println(F("===Transmitting==="));
    delay(300);
    Serial.println(F("===Transmitting==="));
    delay(300);

    // Once completed token holder duties (updating the rest of nodes with new sensor data) -> Time to hand token over to next node.
    if (tokenHolderIndex < (existingNodesCount - 1)) {
      tokenHolderIndex++;  // Give token away to next node.
    } else {
      tokenHolderIndex = 0;  // Reset token counter back to first node in existing.
    }
    printExistingNodes();

    Serial.print(F("Token Index to send: "));
    Serial.print(tokenHolderIndex);
    Serial.print(F(". Its corresponding Token ID is: "));
    Serial.println(existingNodesArray[tokenHolderIndex]);

    Message notifyTokenChange;
    notifyTokenChange.type = 4;
    strcpy(notifyTokenChange.payload, String(tokenHolderIndex).c_str());

    bool sentHandoverSuccessfully = false;
    uint8_t handoverRetryCounter = 0;

    while (!sentHandoverSuccessfully && handoverRetryCounter < RETRY_COUNT) {
      if (manager.sendtoWait((uint8_t *)&notifyTokenChange, sizeof(notifyTokenChange), existingNodesArray[tokenHolderIndex]) == RH_ROUTER_ERROR_NONE) {
        Serial.println(F("Successfully handover token!"));
        sentHandoverSuccessfully = true;
        tokenPosession = 0;
      } else {
        Serial.println(F("Token handover failed. Retrying..."));
        handoverRetryCounter++;
        delay(RETRY_DELAY);
      }
    }

    if (!sentHandoverSuccessfully) {
      Serial.println(F("Token handover failed after maximum retries. Will skip this node for now."));
      deviceDownCounter++;
      Serial.print(3 - deviceDownCounter);
      Serial.println(F(" More attempts before resetting LoRa"));

      // Remove device's token & assume it to be a dead node. Reset it to be a "new" node again.
      if (deviceDownCounter == 3) {
        resetExistingNodes();
        tokenInNetwork = 0;
        deviceDownCounter = 0;
        tokenPosession = 0;
      }
    }
  }

  /* RX MODE: tokenPoession is not held by this node; Actively listens for any messages */
  Serial.println(F("Listening"));

  uint8_t recievedMessageBuf[MAX_PAYLOAD_SIZE];  // Buffer which we use to store any recievedMessages into. Reduce occupying of dynamic memory.
  uint8_t recievedMessageBufLen = sizeof(recievedMessageBuf);
  uint8_t from;

  if (manager.recvfromAck(recievedMessageBuf, &recievedMessageBufLen, &from)) {
    Serial.print(F("Message From : 0x"));
    Serial.println(from, HEX);

    deviceDownCounter = 0;

    // Extracting message from received buffer
    Message *recievedMessage = (Message *)recievedMessageBuf;
    Serial.print(F("Type: "));
    Serial.print(recievedMessage->type);
    Serial.print(F(" Payload: "));
    Serial.println((char *)recievedMessage->payload);

    if (tokenInNetwork) {

      /* If theres a token in network, you can listen for all types of messages. */
      /* A switch is used to ensure message reliability. Ensure the system is in a safe state and that it will not get corrupted by any noise. */
      switch (recievedMessage->type) {
        case 1:  // Type 1 : Indicates that it recieved a neighbour discovery message. Add the sender address to a pendingList.
          Serial.println(F("You have recieved a request to join network. KIV & Process it later."));
          pendingJoinNode = from;
          break;

        case 2:  // Type 2: Recieved an update to the existing_node array.
          Serial.println(F("You have recieved a new update to existingNodesArray!"));
          outdatedRouteFlag = 1;
          pendingJoinNode = 0;
          rememberExistingNodes((char *)recievedMessage->payload);
          Serial.println(F("Array has been updated!"));
          printExistingNodes();
          break;

        case 3:  // Type 3: Handshake to establish a route table
          Serial.println(F("You have recieved a handshake."));
          // No need to do anything here. it was for other party to explictly establish route to you.
          break;

        case 4:  // Type 4: Token Exchange; You are now recieving the token/rights to transmit.
          Serial.println(F("You have recieved a Token Handover Message!"));
          lastHeardTime = millis();
          tokenHolderIndex = atoi(recievedMessage->payload);
          tokenPosession = 1;
          break;

        case 5:  // Type 5: Sensor Data Update; You are now recieving a new set of table data.
          Serial.println(F("RECIEVED A SENSOR TABLE"));
          unserializeSensorTable(recievedMessage->payload);
          printSensorTable();
          break;
      }

    } else {
      // Safety Mechanism to ensure that if node is new and in the process of looking for neighbours, Only allowed them to listen for 2 types of messages (update existing_node array && im new, add me)
      Serial.println(F("Joining a network!"));

      switch (recievedMessage->type) {
        case 1:  // Type 1: You are first node in network && recieve broadcast msg from another node AKA first pair in network.
          // Send type 2 msg. Add yourself to the existingNodeArray & the sender's node address at the end of existingNodeArray & reply the node with the updateExistingNodeArray.
          outdatedRouteFlag = 1;
          existingNodesArray[existingNodesCount++] = nodeID;  // Add the new node into our existing node list.
          existingNodesArray[existingNodesCount++] = from;    // Add the new node into our existing node list.

          Message addedNeighbourMsg;
          addedNeighbourMsg.type = 2;  // Reply with message type '2' to indicate a reply containing existing node list.
          strcpy(addedNeighbourMsg.payload, serializeExistingNodes().c_str());
          printExistingNodes();

          if (manager.sendtoWait((uint8_t *)&addedNeighbourMsg, sizeof(addedNeighbourMsg), from) == RH_ROUTER_ERROR_NONE) {
            Serial.println(F("Replied Neighbour with array!"));
            // delay(1000);
            // After that become token holder.
            outdatedRouteFlag = 1;
            tokenHolderIndex = 0;  // In this case, tokenHolderIndex will be yourself.
            tokenInNetwork = 1;    // Update state of tokenInNetwork
            tokenPosession = 1;
          } else {
            Serial.println(F("Neighbour Reply has failed."));
          }

          break;

        case 2:  // Type 2: Update to the existing_node array.
          outdatedRouteFlag = 1;
          Serial.print(F("Received Type 2 message. Payload:"));
          Serial.println((char *)recievedMessage->payload);  // Print the payload to check its content
          rememberExistingNodes((char *)recievedMessage->payload);
          printExistingNodes();
          tokenInNetwork = 1;  // Update state of tokenInNetwork
          break;
      }
    }
  }
  // // pollSensors();
  // /* IR sensor (GPIO 3) */
  // int irState = digitalRead(IR_SENSOR_PIN);
  // if (irState == HIGH) {
  //   paxCounter++;
  //   delay(300);
  // }
  // sensorTable[nodeID - 1].paxData = paxCounter;
  delay(100);
}


/* printExistingNodes Function - Prints our Existing neighbours */
void printExistingNodes() {
  Serial.print(F("Existing nodes: "));
  for (int i = 0; i < existingNodesCount; i++) {
    Serial.print(existingNodesArray[i]);
    Serial.print(F(" "));
  }
  Serial.println();
}

/* serializeExistingNodes Function - Serializes our existing nodes array & pass it over to another node. Ensures other nodes knows who is in the network */
String serializeExistingNodes() {
  // Start with an empty string
  String existingNodesArrayString = "";

  // Iterate over the array and concatenate each value to the string
  for (int i = 0; i < existingNodesCount; i++) {
    // Convert the uint8_t value to a string and append it to the existing_nodes_string
    existingNodesArrayString += String(existingNodesArray[i]);

    // Add a comma separator if it's not the last element
    if (i < existingNodesCount - 1) {
      existingNodesArrayString += ",";
    }
  }
  return existingNodesArrayString;
}

/* rememberExistingNodes Function - Appends into our existing nodes array */
void rememberExistingNodes(char *buf) {
  // Copy the payload buffer to a temporary buffer and append a null character
  char tempBuf[20];
  memcpy(tempBuf, buf, 20 - 1);
  tempBuf[20 - 1] = '\0';

  // Parse the payload into array
  char *token = strtok(tempBuf, ",");
  while (token != NULL) {
    // Convert the token to an integer to store into our array
    int node = atoi(token);

    // Check if the node already exists in the existing_nodes array
    bool found = false;
    for (int i = 0; i < existingNodesCount; i++) {
      if (existingNodesArray[i] == node) {
        found = true;
        break;
      }
    }

    /* Due to limitation of memory capacity, we set it to MAX_NODES so the total can only contain 5 nodes! */
    /* Unlikely to enter this loop as we are simulating with only 5 nodes. However, for a scalable design, we added these codes */
    if (!found) {
      if (existingNodesCount < MAX_NODES) {
        existingNodesArray[existingNodesCount++] = node;
      } else {
        Serial.println(F("Existing nodes array is full."));
        break;
      }
    }

    token = strtok(NULL, ",");  // Get the next token
  }
}

/* establishRouteToNeighbours Function - Establish a handshake which makes sure that a route is established in our meshed network for faster communication/prefered routes of 'next hop' */
void establishRouteToNeighbours() {
  Message handshakeAndEstablishMsg;
  handshakeAndEstablishMsg.type = 3;  // Send specific node with message type '3' to establish handshake & setup route table.
  strcpy(handshakeAndEstablishMsg.payload, "Best Route?");
  delay(100);

  for (uint8_t i = 0; i < existingNodesCount; i++) {
    if (existingNodesArray[i] == nodeID) {
      continue;
    }
    if (manager.sendtoWait((uint8_t *)&handshakeAndEstablishMsg, sizeof(handshakeAndEstablishMsg), existingNodesArray[i]) == RH_ROUTER_ERROR_NONE) {
      Serial.println(F("Sent Routing Message!"));
    } else {
      Serial.println(F("Error Routing"));
    }
    delay(500);
  }
  manager.printRoutingTable();
  delay(200);
  Serial.println();
  outdatedRouteFlag = 0;
}

/* pollSensors Function - Poll our sensors and store it into its own node ID's data row in the table. */
void pollSensors() {
  sensorTable[nodeID - 1].nodeID = nodeID;

  /* Temperature & Humidity Sensor (GPIO 8) */
  int chk = DHT11.read(DHT11_PIN);
  sensorTable[nodeID - 1].tempData = DHT11.temperature;
  sensorTable[nodeID - 1].humidityData = DHT11.humidity;

  /* Smoke Sensor (GPIO A0) */
  float res = resistance(5, 50);
  res /= Res;
  sensorTable[nodeID - 1].smokeData = pow(10, (((log(res) - SmokeCurve[1]) / SmokeCurve[2]) + SmokeCurve[0]));

  // /* IR sensor (GPIO 3) */
  // int irState = digitalRead(IR_SENSOR_PIN);
  // if (irState == HIGH) {
  //   paxCounter++;
  //   delay(300);
  // }
  // sensorTable[nodeID - 1].paxData = paxCounter;

  /* This below portion is where we check for fire. If sensor data are abnormnally high (above threshold), take it as fire detected & broadcast a fire message */
  // if (DHT11.humidity <= HUMIDITY_THRESHOLD && DHT11.temperature >= TEMPERATURE_THRESHOLD && SmokeResult >= SMOKE_THRESHOLD) {
  if (sensorTable[nodeID - 1].tempData >= 35) {  // For demo purposes, we only used temp data to simulate fire as it may be hard to simulate the gas data in school.
    sensorTable[nodeID - 1].tempData = 255;
    sensorTable[nodeID - 1].humidityData = 255;
    sensorTable[nodeID - 1].smokeData = 255;
    sensorTable[nodeID - 1].paxData = 255;
  }
}

/* resistance Function - Used for calibrating Smoke Sensor */
float resistance(int samples, int interval) {
  int i;
  float res = 0;
  for (i = 0; i < samples; i++) {
    int adc_value = analogRead(SMOKE_SENSOR_PIN);
    res += ((float)load_Res * (1023 - adc_value) / adc_value);
    delay(interval);
  }
  res /= samples;
  return res;
}

/* SensorCalibration Function - Calibrates our smoke sensor */
float SensorCalibration() {
  float val = 0;
  val = resistance(50, 500);
  val /= air_factor;
  return val;
}

/* update_broadcast_sensor_table Function - Reliabily sends the device's latest sensor data to all existing nodes in our network */
void update_broadcast_sensor_table() {
  Message sensorDataMsg;
  sensorDataMsg.type = 5;  // Send message type '5' to signify sensor table.
  delay(100);
  serializeSensorTable(sensorDataMsg.payload);
  sendSerializedDataToNodeMCU(sensorDataMsg.payload, sizeof(sensorDataMsg.payload));

  // Send sensor data table to all nodes except itself.
  for (uint8_t i = 0; i < existingNodesCount; i++) {
    if (existingNodesArray[i] != nodeID) {
      uint8_t retries = 3;  // A retry mechanism to ensure message delivery.
      while (retries > 0) {
        if (manager.sendtoWait((uint8_t *)&sensorDataMsg, sizeof(sensorDataMsg), existingNodesArray[i]) == RH_ROUTER_ERROR_NONE) {
          Serial.print(F("Sent Updated Sensor Table to node: "));
          Serial.println(existingNodesArray[i]);
          break;
        } else {
          retries--;  // Decrement the retry counter
          Serial.print(F("Sensor table sending failed to node: "));
          Serial.println(existingNodesArray[i]);
          Serial.println(F("Retrying..."));
          delay(RETRY_DELAY);
        }
      }
      if (retries == 0) {
        Serial.print(F("Exceeded Max Times (3) of sensor broadcast to node: "));
        Serial.println(existingNodesArray[i]);
        outdatedRouteFlag = 1;
      }
    }
  }
  printSensorTable();
}

/* sendSerializedDataToNodeMCU Function - Update the node MCU with new, latest, sensor data */
void sendSerializedDataToNodeMCU(uint8_t *serializedData, size_t dataSize) {
  // Ensure the SoftwareSerial connection is open
  if (!ArduinoUno) {
    Serial.println(F("SoftwareSerial connection not open."));
    return;
  }
  Serial.println(F("sent"));

  // Send the size of the data first
  ArduinoUno.write((uint8_t *)&dataSize, sizeof(dataSize));
  delay(10);  // Short delay to ensure data is sent

  // Send the serialized data
  ArduinoUno.write(serializedData, dataSize);
  delay(10);  // Short delay to ensure data is sent

  Serial.println(F("Data sent to NodeMCU.\n"));
}

/* printSensorTable Function - Prints updated sensor table */
void printSensorTable() {
  Serial.println(F("Sensor Data Table:"));
  for (int i = 0; i < MAX_NODES; i++) {
    Serial.print(F("Node ID: "));
    Serial.print(sensorTable[i].nodeID);
    Serial.print(F(", Temperature: "));
    Serial.print(sensorTable[i].tempData);  // Unit: Degree Celcius
    Serial.print(F(", Pax: "));
    Serial.print(sensorTable[i].paxData);
    Serial.print(F(", Smoke: "));
    Serial.print(sensorTable[i].smokeData);  // Unit: ppm
    Serial.print(F(", Humidity: "));
    Serial.println(sensorTable[i].humidityData);  // Unit: percent
  }
}

/* serializeSensorTable Function - Compresses our sensor table to send across to other nodes. (Prevents buffer overflow) */
void serializeSensorTable(uint8_t *buffer) {
  for (int i = 0; i < MAX_NODES; ++i) {
    memcpy(buffer + i * SENSOR_DATA_SIZE, &sensorTable[i], SENSOR_DATA_SIZE);
  }
}

/* unserializeSensorTable Function - Uncompresses the serialized (compressed) data & update the neighbour's sensor data record in own node's table */
void unserializeSensorTable(const uint8_t *buffer) {
  for (int i = 0; i < MAX_NODES; ++i) {

    // Unserailizing of sensor table data
    uint8_t receivedNodeID = buffer[i * SENSOR_DATA_SIZE];
    uint8_t tempData = buffer[i * SENSOR_DATA_SIZE + 1];
    uint8_t paxData = buffer[i * SENSOR_DATA_SIZE + 2];
    uint8_t smokeData = buffer[i * SENSOR_DATA_SIZE + 3];
    uint8_t humidityData = buffer[i * SENSOR_DATA_SIZE + 4];

    // Check if the received nodeID is the same as the device's nodeID
    if (receivedNodeID != nodeID) {
      // Check if the incoming data is not 0 0 0 0  which implies no data for that node is present yet. (it may be outdated hence no action required; Prevents overwriting existing data with old data)
      if (tempData != 0 || paxData != 0 || smokeData != 0 || humidityData != 0) {
        sensorTable[i].nodeID = receivedNodeID;  // If it's the device's own nodeID, update only the nodeID in the sensor table so we dont have node 0 that makes it look like its non existent.
        memcpy(&sensorTable[i], buffer + i * SENSOR_DATA_SIZE, SENSOR_DATA_SIZE);
      } else {
        sensorTable[i].nodeID = receivedNodeID;  // If it's the device's own nodeID, update only the nodeID in the sensor table so we dont have node 0 that makes it look like its non existent.
      }
    }
  }
  pollSensors();
}

/* resetExistingNodes - A failure mechanism which ensures the resilience of our mesh network. If token holder repeatedly retries to give token away but fails, kick itself out of network & reset itself. */
void resetExistingNodes() {
  // Loop through the existingNodesArray and set each element to 0
  for (int i = 0; i < MAX_NODES; i++) {
    existingNodesArray[i] = 0;
  }
  // Reset the count of existing nodes
  existingNodesCount = 0;
}