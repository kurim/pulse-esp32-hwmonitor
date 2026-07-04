"""
Beispiel-Bridge: liest CPU-Werte (und optional GPU via pynvml/pyadl) aus und
published sie als JSON per MQTT an das ESP32-CYD-Board.

Dies ist nur ein Startpunkt - für echte CPU/GPU-Temperaturen und Leistungsaufnahme
unter Windows i.d.R. LibreHardwareMonitor (mit aktivierter Remote-Web-API) oder
HWiNFO64 (Shared Memory Support) als Datenquelle nutzen statt psutil.

Abhängigkeiten:
    pip install paho-mqtt psutil requests

Nutzung:
    python pc_bridge_example.py --host 192.168.1.50 --topic pcbridge/hwinfo
"""
import argparse
import json
import time

import psutil
import paho.mqtt.client as mqtt


def read_metrics():
    """Liefert die per CYD erwarteten Felder.
    cpu_temp/cpu_power/gpu_* hier nur als Platzhalter - durch eine echte
    Quelle ersetzen (z.B. LibreHardwareMonitor REST-API, HWiNFO64 Shared Memory).
    """
    cpu_load = psutil.cpu_percent(interval=None)

    cpu_temp = 0.0
    try:
        temps = psutil.sensors_temperatures()
        if temps:
            first_sensor = next(iter(temps.values()))
            if first_sensor:
                cpu_temp = first_sensor[0].current
    except (AttributeError, NotImplementedError):
        pass  # unter Windows liefert psutil i.d.R. keine Temperaturen

    return {
        "cpu_load": round(cpu_load, 1),
        "cpu_temp": round(cpu_temp, 1),
        "cpu_power": 0.0,   # TODO: aus LibreHardwareMonitor/HWiNFO64 ersetzen
        "gpu_load": 0.0,    # TODO
        "gpu_temp": 0.0,    # TODO
        "gpu_power": 0.0,   # TODO
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, help="MQTT-Broker-Adresse")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--topic", default="pcbridge/hwinfo")
    parser.add_argument("--user", default=None)
    parser.add_argument("--password", default=None)
    parser.add_argument("--interval", type=float, default=2.0, help="Sekunden zwischen Updates")
    args = parser.parse_args()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    if args.user:
        client.username_pw_set(args.user, args.password)
    client.connect(args.host, args.port, keepalive=30)
    client.loop_start()

    print(f"Verbunden mit {args.host}:{args.port}, publiziere auf '{args.topic}' alle {args.interval}s")
    try:
        while True:
            payload = json.dumps(read_metrics())
            client.publish(args.topic, payload, qos=0, retain=False)
            time.sleep(args.interval)
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()