# Internet Of Things Submission Repo (Team 28)
Building Fire Monitoring & Evacuation System (Utilizing LoRa Mesh)

## Team Members:
1. Tan Wen Jie Nicolas 2203432
2. Clement Choo Jun Kai 2202587
3. Lee Jian Jie 2202094
4. Veleon Lim Ming Zhe 2201947
5. Pyrena Chua Yi Zhen 2203502

### Contents of Git Repository
1. MakerUno x LoRa RFM Shield Source Codes
2. Flask Web Application (Dashboard)
3. NodeMCU Source Codes

### Link to demo:
https://youtu.be/m5AcK2arDOA


### Pinout Diagram:
![Screenshot 2024-04-08 221821](https://github.com/purpies/T28_IOT_LoRa/assets/41365269/7d3da2c4-69b6-41c6-93ad-2860ed98623f)


### Setup Instructions:
1. Flash MakerUno_Lora codes into each lora device. (Each device nodeID must be unique).
Hence, we have provided 3 copies for 3 seperate lora devices AKA "nodes".
We have done testing up to 4 working nodes and expected to be able to scale up even further. (Should you require to perform testing with more nodes, increase accordingly and make sure to update the nodeID variable in the codes.
2. Flash NodeMCU codes into NodeMCU (Used for LoRa nodes to send sensor table data via serial to be published onto our MQTT broker
3. Startup RPi which is our MQTT broker.
4. Start a MQTT Broker on the Raspberry Pi.
5. Start up web application, which is already subscribed to broker to retrieve any sensor table.
6. Connect Sensors, NodeMCU into the LoRa Shield.
7. Turn LoRa nodes on, 1 at a time, & only proceed to turn a subsequent device when they are successfully turned on & connected. This is to ensure proper setup process.
8. System is now ready to use.

