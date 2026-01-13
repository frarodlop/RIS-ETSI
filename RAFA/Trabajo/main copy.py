#!/usr/bin/env python3

""" MQTT exporter """

import json
import re
import signal
import logging
import os
import sys
import serial
import time

import paho.mqtt.client as mqtt
from prometheus_client import Counter, Gauge, start_http_server

# Environment configuration 
MQTT_ADDRESS = os.getenv("MQTT_ADDRESS")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))

# Prometheus configuraiton
prom_msg_counter = None
prom_temp_c_gauge = None
prom_temp_f_gauge = None
prom_temp_avg_c_gauge = None
prom_temp_avg_f_gauge = None
prom_alarm_gauge = None

# Create logging
logging.basicConfig(filename='/mqtt_exporter/log/register.log', level=logging.DEBUG)
LOG = logging.getLogger("[mqtt-exporter]")

# ---- Conversiones de temperatura ----
def c_to_f(temp_c):
    return temp_c * 9 / 5 + 32

def f_to_c(temp_f):
    return (temp_f - 32) * 5 / 9


# ---- Variables globales para promedio ----
temp_c_total = 0.0
temp_f_total = 0.0
temp_count = 0

def update_averages(temp_c, temp_f):
    global temp_c_total, temp_f_total, temp_count
    temp_c_total += temp_c
    temp_f_total += temp_f
    temp_count += 1
    avg_c = temp_c_total / temp_count
    avg_f = temp_f_total / temp_count
    return avg_c, avg_f

# Handlers
def create_msg_counter_metrics():
    global prom_msg_counter
    LOG.info("Created counter for message number")
    prom_msg_counter = Counter( 'number_msgs',
        'Number of received messages')

def create_temp_c_gauge_metrics():
    global prom_temp_c_gauge
    LOG.info("Created gauge for temperature Degrees")
    prom_temp_c_gauge = Gauge( 'temp_c',
        'Temperature [Celsius Degrees]')
    
def create_temp_f_gauge_metrics():
    global prom_temp_f_gauge
    LOG.info("Created gauge for temperature Fahrenheit")
    prom_temp_f_gauge = Gauge( 'temp_f',
        'Temperature [Fahrenheit Degrees]')
    
def create_temp_c_avg_gauge_metrics():
    global prom_temp_avg_c_gauge
    LOG.info("Created gauge for average temperature Degrees")
    prom_temp_avg_c_gauge = Gauge( 'temp_avg_c',
        'Temperature [Celsius Degrees]')
    
def create_temp_f_avg_gauge_metrics():
    global prom_temp_avg_f_gauge
    LOG.info("Created gauge for average temperature Fahrenheit")
    prom_temp_avg_f_gauge = Gauge( 'temp_avg_f',
        'Temperature [Fahrenheit Degrees]')
    
def create_alarm_metrics():
    global prom_alarm_gauge
    LOG.info("Created gauge for alarm state")
    prom_alarm_gauge = Gauge( 'alarm',
        'State [0=OFF, 1=ON]'
    )

def process_serial_line(line):
    # Solo procesar si hay un ';'
    if b';' not in line:
        return None

    try:
        decoded = line.decode('ascii', errors='replace').strip()
        parts = [p.strip() for p in decoded.split(';')]

        if len(parts) != 5:
            LOG.warning("Incomplete serial line: %s", decoded)
            return None

        node_id, unidad_temp, temp_str, id_alarm, alarm_str = parts

        node_id = int(node_id)
        unidad_temp = int(unidad_temp)      # 1=C, 2=F
        temp_val = float(temp_str)
        alarm_id = int(id_alarm)
        alarm_state = int(alarm_str)

        # Normalizar temperatura
        if unidad_temp == 1:
            temp_c = temp_val
            temp_f = c_to_f(temp_c)
        elif unidad_temp == 2:
            temp_f = temp_val
            temp_c = f_to_c(temp_f)
        else:
            LOG.error("Unknown unit: %s", unidad_temp)
            return None

        # Actualizar promedio por nodo
        temp_avg_c, temp_avg_f = update_avg(node_id, temp_c, temp_f)

        # Publicar MQTT por nodo
        client.publish(f"Temp_c/{node_id}", f"{temp_c:.2f}", qos=0, retain=False)
        client.publish(f"Temp_f/{node_id}", f"{temp_f:.2f}", qos=0, retain=False)
        client.publish(f"Temp_avg_c/{node_id}", f"{temp_avg_c:.2f}", qos=0, retain=False)
        client.publish(f"Temp_avg_f/{node_id}", f"{temp_avg_f:.2f}", qos=0, retain=False)
        client.publish(f"Alarm/{node_id}/{alarm_id}", str(alarm_state), qos=0, retain=False)

        LOG.info(
            "Node %d Published: C=%.2f F=%.2f avgC=%.2f avgF=%.2f alarm_id=%d state=%d",
            node_id, temp_c, temp_f, temp_avg_c, temp_avg_f, alarm_id, alarm_state
        )

    except Exception as e:
        LOG.error("Failed to process serial line '%s': %s", line, e)

def parse_message(raw_topic, raw_payload):
    try:
        payload = json.loads(raw_payload)
    except json.JSONDecodeError:
        LOG.error(" Failed to parse payload as JSON: %s", str(payload, 'ascii'))
        return None, None
    except UnicodeDecodeError:
        LOG.error(" Encountered undecodable payload: %s", raw_payload)
        return None, None
    
    topic = raw_topic

    return topic, payload

def parse_metric(data):
    if isinstance(data, (int,float)):
        return data
    
    if isinstance(data, bytes):
        data = data.decode()

    if isinstance(data, str):
        data = data.upper()

    return float(data)

def parse_metrics(data, topic, client_id):
    for metric, value in data.items():
        if isinstance(value, dict):
            LOG.debug(" Parsing dict %s, %s", metric, value)
            parse_metrics(value, topic, client_id)
            continue

        try:
            metric_value = parse_metric(value)
        except ValueError as err:
            LOG.error(" Failed to convert %s, Error: %s", metric, err)

def on_connect(client, userdata, flags, rc, properties=None):
    LOG.info("Connected with result code: %s", rc)
    print("Connected with result code:", rc)

    if rc == mqtt.CONNACK_ACCEPTED:
        # Suscribirse a todos los tópicos relevantes
        client.subscribe("Temp_C/#")        # Todas las temperaturas en Celsius por nodo
        client.subscribe("Temp_F/#")        # Todas las temperaturas en Fahrenheit por nodo
        client.subscribe("Temp_avg_C/#")    # Promedios en Celsius
        client.subscribe("Temp_avg_F/#")    # Promedios en Fahrenheit
        client.subscribe("Alarm/#")         # Alarmas por nodo y tipo
        LOG.info("Subscribed to all relevant topics")
    else:
        LOG.error("[ERROR]: MQTT connection refused, code %s", rc)


def on_message(client, userdata, msg):
    LOG.info(" [Topic: %s] %s", msg.topic, msg.payload)

    topic, payload = parse_message(msg.topic, msg.payload)
    LOG.debug(" \t Topic: %s", topic)
    LOG.debug(" \t Payload: %s", payload)

    if not topic or not payload:
        LOG.error(" [ERROR]: Topic or Payload not found")
        return

    prom_msg_counter.inc()
   
    if(msg.topic == "Temp_c"):                    # celsius
        prom_temp_c_gauge.set(payload)
    elif(msg.topic == "Temp_f"):                  # fahrenheit
        prom_temp_f_gauge.set(payload)
    elif(msg.topic == "Temp_avg_c"):              # average celsius
        prom_temp_avg_c_gauge.set(payload)
    elif(msg.topic == "Temp_avg_f"):              # average fahrenheit
        prom_temp_avg_f_gauge.set(payload)
    elif(msg.topic == "Alarm"):                   # alarm
        prom_alarm_gauge.set(payload)      
     
    LOG.info("Payload %s", payload)

def main():

    # Create MQTT client
    client =mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
   
    # Create Prometheus metrics
    create_msg_counter_metrics()
    create_temp_c_gauge_metrics()
    create_temp_f_gauge_metrics()
    create_temp_c_avg_gauge_metrics()
    create_temp_f_avg_gauge_metrics()
    create_alarm_metrics()

    # Start prometheus server
    start_http_server(9000)

    # Configure MQTT topic
    client.on_connect = on_connect
    client.on_message = on_message
    
    # Suscribe MQTT topics
    LOG.debug(" Connecting to Mosquitto container ")
    while True:
        try:
            LOG.debug("Address: %s", str(MQTT_ADDRESS))
            LOG.debug("Port: %s", str(MQTT_PORT))
            client.connect(MQTT_ADDRESS, MQTT_PORT, 60)
            break
        except Exception:
            print("MQTT client could not connect to Mosquitto", flush=True)
            LOG.error("MQTT client could not connect to Mosquitto")
            time.sleep(5)
    
    # Waiting for messages
    client.loop_start()

    #Serial port
    try:
        ser = serial.Serial(port="/dev/ttyACM0",
                            baudrate=115200,
                            parity=serial.PARITY_NONE,
                            stopbits=serial.STOPBITS_ONE,
                            bytesize=serial.EIGHTBITS,
                            timeout=2000)
        ser.flushInput()
        ser.flush()
        ser.isOpen()
        LOG.info("Serial Port /dev/ACM0 is opened")
    except IOError:
        LOG.error("serial port is already opened or does not exist")
        LOG.close()
        sys.exit(0)

    while True:
        # Reading from serial port
        line = ser.readline()
        decoded = line.decode('utf-8', errors='ignore').rstrip()

        # Print data received
        LOG.debug("Serial Data: %s", decoded)
        print("Serial Data:", decoded)

        # Process the serial line
        process_serial_line(line)



if __name__ == "__main__":
    main()