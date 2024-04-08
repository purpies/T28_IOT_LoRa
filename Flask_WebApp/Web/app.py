from flask import Flask, jsonify, render_template
import paho.mqtt.client as mqtt

app = Flask(__name__)

# MQTT broker details
broker = "192.168.34.222"
port = 1883 # Default MQTT port
topic = "your/topic"

# Variables to store the latest received message
node_id_1, temperature_1, pax_1, smoke_1, humidity_1, node_id_2, temperature_2, pax_2, smoke_2, humidity_2, node_id_3, temperature_3, pax_3, smoke_3, humidity_3, node_id_4, temperature_4, pax_4, smoke_4, humidity_4 = None, None, None, None, None, None, None, None, None, None, None, None, None, None, None, None, None, None, None, None


def on_message(client, userdata, msg):
    global node_id_1, temperature_1, pax_1, smoke_1, humidity_1, node_id_2, temperature_2, pax_2, smoke_2, humidity_2, node_id_3, temperature_3, pax_3, smoke_3, humidity_3, node_id_4, temperature_4, pax_4, smoke_4, humidity_4
    # Check if the payload is not empty
    payload = msg.payload.decode().strip()
    print(payload)
    if payload:
        # Split the payload into a list of numbers
        numbers = payload.split()

        # Initialize a set to keep track of unique node IDs
        unique_node_ids = set()
        
        # Filter out duplicates of node IDs 1 through 5
        filtered_numbers = []
        for number in numbers:
            node_id = int(number)
            if 1 <= node_id <= 5:
                if node_id not in unique_node_ids:
                    unique_node_ids.add(node_id)
                    filtered_numbers.append(number)
            else:
                filtered_numbers.append(number)
        
        # Now, filtered_numbers contains the payload without duplicates of node IDs 1 through 5
        print("Filtered numbers:", filtered_numbers) # Add this line to see the filtered numbers
        
        # Parse the filtered payload
        i = 0
        while i < len(filtered_numbers):
            node_id = int(filtered_numbers[i])
            if 1 <= node_id <= 5: # Check if the number is a valid node ID
                # Assign sensor data to the appropriate variables
                node_data = filtered_numbers[i:i+5]
                # Convert specific values to "Fire" if they are 255
                for index, value in enumerate(node_data):
                    if index == 3 and value == "255": # Assuming smoke is the 4th value in the node data
                        node_data[index] = "FIRE"
                    if index == 1 and value == "255": # Assuming smoke is the 4th value in the node data
                        node_data[index] = "FIRE"   
                    if index == 4 and value == "255": # Assuming smoke is the 4th value in the node data
                        node_data[index] = "FIRE"    
                    if index == 2 and value == "255": # Assuming smoke is the 4th value in the node data
                        node_data[index] = "FIRE"     
                    # Add similar checks for humidity, pax, and temperature if needed
                # Example: Convert humidity to "High" if it's 85
                # if node_data[4] == "85": # Assuming humidity is the 5th value in the node data
                #     node_data[4] = "High"
                # Assign the processed node data to the global variables
                if node_id == 1:
                    node_id_1, temperature_1, pax_1, smoke_1, humidity_1 = node_data
                elif node_id == 2:
                    node_id_2, temperature_2, pax_2, smoke_2, humidity_2 = node_data
                elif node_id == 3:
                    node_id_3, temperature_3, pax_3, smoke_3, humidity_3 = node_data
                elif node_id == 4:
                    node_id_4, temperature_4, pax_4, smoke_4, humidity_4 = node_data
                # Increment i by 5 to move to the next node
                i += 5
            else:
                # If node ID is not in the range of 1 to 5, move to the next number
                i += 1
        print(f"Received message: Topic: {msg.topic}, Payload: {payload}")
        # Print all variables
        print(f"Node 1: ID: {node_id_1}, Temperature: {temperature_1}, Pax: {pax_1}, Smoke: {smoke_1}, Humidity: {humidity_1}")
        print(f"Node 2: ID: {node_id_2}, Temperature: {temperature_2}, Pax: {pax_2}, Smoke: {smoke_2}, Humidity: {humidity_2}")
        print(f"Node 3: ID: {node_id_3}, Temperature: {temperature_3}, Pax: {pax_3}, Smoke: {smoke_3}, Humidity: {humidity_3}")
        print(f"Node 4: ID: {node_id_4}, Temperature: {temperature_4}, Pax: {pax_4}, Smoke: {smoke_4}, Humidity: {humidity_4}")
        # Add print statement for Node 5 if you have it
    else:
        print(f"Ignored message with empty payload: Topic: {msg.topic}")


# Create MQTT client
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
client.on_message = on_message

# Connect to MQTT broker
client.connect(broker, port)

# Subscribe to topic
client.subscribe(topic)

@app.route('/')
def index():
    # Pass the latest message to the template
    return render_template('index.html', node_id_1=node_id_1, temperature_1=temperature_1, pax_1=pax_1, smoke_1=smoke_1, humidity_1=humidity_1, node_id_2=node_id_2, temperature_2=temperature_2, pax_2=pax_2, smoke_2=smoke_2, humidity_2=humidity_2, node_id_3=node_id_3, temperature_3=temperature_3, pax_3=pax_3, smoke_3=smoke_3, humidity_3=humidity_3, node_id_4=node_id_4, temperature_4=temperature_4, pax_4=pax_4, smoke_4=smoke_4, humidity_4=humidity_4)




if __name__ == '__main__':
    # Start MQTT loop
    client.loop_start()
    app.run(debug=True)
