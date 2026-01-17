#!/usr/bin/env python3

""" MQTT exporter """

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

# MQTT client global
client = None

# Prometheus metrics
prom_msg_counter = None
prom_temp_c_gauge = None
prom_temp_f_gauge = None
prom_temp_c_avg_gauge = None
prom_temp_f_avg_gauge = None
prom_alarm_gauge = None

# Logging
logging.basicConfig(filename='/mqtt_exporter/log/register.log', level=logging.DEBUG)
LOG = logging.getLogger("[mqtt-exporter]")

# ---- Conversiones ----
def c_to_f(temp_c):
    return temp_c * 9 / 5 + 32

def f_to_c(temp_f):
    return (temp_f - 32) * 5 / 9

# ---- Variables para promedios ----
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

# ---- Prometheus metric creators ----
def create_msg_counter_metrics():
    global prom_msg_counter
    prom_msg_counter = Counter('number_msgs', 'Number of received messages')

def create_temp_c_gauge_metrics():
    global prom_temp_c_gauge
    prom_temp_c_gauge = Gauge('temp_c', 'Temperature [Celsius Degrees]')

def create_temp_f_gauge_metrics():
    global prom_temp_f_gauge
    prom_temp_f_gauge = Gauge('temp_f', 'Temperature [Fahrenheit Degrees]')

def create_temp_c_avg_gauge_metrics():
    global prom_temp_c_avg_gauge
    prom_temp_c_avg_gauge = Gauge('temp_c_avg', 'Average Temperature [Celsius Degrees]')

def create_temp_f_avg_gauge_metrics():
    global prom_temp_f_avg_gauge
    prom_temp_f_avg_gauge = Gauge('temp_f_avg', 'Average Temperature [Fahrenheit Degrees]')

def create_alarm_metrics():
    global prom_alarm_gauge
    prom_alarm_gauge = Gauge('alarm', 'Alarm state [0=OFF, 1=ON]')

# ---- Procesar línea del serial ----
def process_serial_line(line):
    global client
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
        unidad_temp = int(unidad_temp)
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

        # Calcular promedio
        temp_avg_c, temp_avg_f = update_averages(temp_c, temp_f)

        # Publicar MQTT
        client.publish(f"Temp_C/{node_id}", f"{temp_c:.2f}")
        client.publish(f"Temp_F/{node_id}", f"{temp_f:.2f}")
        client.publish(f"Temp_Avg_C/{node_id}", f"{temp_avg_c:.2f}")
        client.publish(f"Temp_Avg_F/{node_id}", f"{temp_avg_f:.2f}")
        client.publish(f"Alarm/{node_id}/{alarm_id}", str(alarm_state))

        LOG.info(
            "Node %d Published: C=%.2f F=%.2f avgC=%.2f avgF=%.2f alarm_id=%d state=%d",
            node_id, temp_c, temp_f, temp_avg_c, temp_avg_f, alarm_id, alarm_state
        )

    except Exception as e:
        LOG.error("Failed to process serial line '%s': %s", line, e)

# ---- MQTT ----
def on_connect(client, userdata, flags, rc, properties=None):
    print("Connected with result:", rc)
    if rc == 0:
        client.subscribe("Temp_C/#")
        client.subscribe("Temp_F/#")
        client.subscribe("Temp_Avg_C/#")
        client.subscribe("Temp_Avg_F/#")
        client.subscribe("Alarm/#")
        LOG.info("Subscribed to topics")
    else:
        LOG.error("Connection refused code %s", rc)

def on_message(client, userdata, msg):
    topic = msg.topic     
    payload = msg.payload.decode()

    prom_msg_counter.inc()

    if topic.startswith("Temp_C/"):
        prom_temp_c_gauge.set(float(payload))

    elif topic.startswith("Temp_F/"):
        prom_temp_f_gauge.set(float(payload))

    elif topic.startswith("Temp_Avg_C/"):
        prom_temp_c_avg_gauge.set(float(payload))

    elif topic.startswith("Temp_Avg_F/"):
        prom_temp_f_avg_gauge.set(float(payload))

    elif topic.startswith("Alarm/"):
        prom_alarm_gauge.set(float(payload))

    LOG.info("Updated metric from topic %s: %s", topic, payload)


# ---- MAIN ----
def main():
    global client

    # MQTT client
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

    # Prometheus metrics
    create_msg_counter_metrics()
    create_temp_c_gauge_metrics()
    create_temp_f_gauge_metrics()
    create_temp_c_avg_gauge_metrics()
    create_temp_f_avg_gauge_metrics()
    create_alarm_metrics()

    start_http_server(9000)

    client.on_connect = on_connect
    client.on_message = on_message

    # Connect to MQTT
    while True:
        try:
            client.connect(MQTT_ADDRESS, MQTT_PORT, 60)
            break
        except Exception:
            LOG.error("Failed to connect to MQTT broker")
            time.sleep(5)

    client.loop_start()

    # Serial
    try:
        ser = serial.Serial("/dev/ttyACM0", 115200, timeout=2)
        LOG.info("Serial port opened")
    except Exception as e:
        LOG.error("Serial error: %s", e)
        sys.exit(1)

    while True:
        line = ser.readline()
        if line:
            process_serial_line(line)

if __name__ == "__main__":
    main()
