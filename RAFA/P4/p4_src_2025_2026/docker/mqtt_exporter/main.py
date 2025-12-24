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
prom_temp_gauge = None

# Create logging
logging.basicConfig(filename='/mqtt_exporter/log/register.log', level=logging.DEBUG)
LOG = logging.getLogger("[mqtt-exporter]")

# Handlers
def create_msg_counter_metrics():
    global prom_msg_counter
    LOG.info("Created counter for message number")
    prom_msg_counter = Counter( 'number_msgs',
        'Number of received messages')

def create_temp_gauge_metrics():
    global prom_temp_gauge
    LOG.info("Created gauge for temperature")
    prom_temp_gauge = Gauge( 'temp_F',
        'Temperature [Farenheit Degrees]')

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
    LOG.info(" Connected with result code: %s", rc)
    client.subscribe("Temp_F")
    if rc != mqtt.CONNACK_ACCEPTED:
        LOG.error("[ERROR]: MQTT %s", rc)

def on_message(client, userdata, msg):
    LOG.info(" [Topic: %s] %s", msg.topic, msg.payload)

    topic, payload = parse_message(msg.topic, msg.payload)
    LOG.debug(" \t Topic: %s", topic)
    LOG.debug(" \t Payload: %s", payload)

    if not topic or not payload:
        LOG.error(" [ERROR]: Topic or Payload not found")
        return

    prom_msg_counter.inc()
    LOG.info("Payload %s", payload)
    prom_temp_gauge.set(payload)

def main():

    # Create MQTT client
    client =mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

    # Create Prometheus metrics
    create_msg_counter_metrics()
    create_temp_gauge_metrics()

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
        line = ser.readline()
        LOG.debug("Serial Data: %s", str(line, 'ascii').rstrip())

        ser_temp=float(line.rstrip())
        print("Temp: %s", str(ser_temp))
        #LOG.debug(str(ser_temp))
        client.publish(topic="Temp_F", payload=ser_temp, qos=0, retain=False)

if __name__ == "__main__":
    main()