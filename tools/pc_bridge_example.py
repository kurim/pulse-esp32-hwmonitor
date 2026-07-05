"""
Beispiel-Bridge: liest CPU-Werte (und optional GPU via pynvml/pyadl) aus und
sendet sie als JSON entweder per MQTT oder per USB/Seriell an das ESP32-Board
(umschaltbar im Webportal des Geräts, siehe README.md).

Dies ist nur ein Startpunkt - für echte CPU/GPU-Temperaturen und Leistungsaufnahme
unter Windows i.d.R. LibreHardwareMonitor (mit aktivierter Remote-Web-API) oder
HWiNFO64 (Shared Memory Support) als Datenquelle nutzen statt psutil.

Abhängigkeiten:
    pip install paho-mqtt psutil requests pyserial

Nutzung (MQTT, Standard):
    python pc_bridge_example.py --host 192.168.1.50 --topic pulsemqtt/hwinfo

Nutzung (USB/Seriell, Gerät muss im Webportal auf "USB/Seriell" gestellt sein):
    python pc_bridge_example.py --serial COM3
    python pc_bridge_example.py --serial /dev/ttyUSB0
"""
import argparse
import json
import time

import psutil


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


def run_mqtt(args):
    import paho.mqtt.client as mqtt

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


def run_serial(args):
    import serial

    # Baudrate ist auf dem Geraet fest auf 115200 verdrahtet (siehe
    # main/net/serial_handler.c), Format: ein JSON-Objekt pro Zeile.
    with serial.Serial(args.serial, 115200) as ser:
        print(f"Verbunden mit {args.serial} (115200 Baud), sende alle {args.interval}s")
        try:
            while True:
                line = json.dumps(read_metrics()) + "\n"
                ser.write(line.encode("utf-8"))
                time.sleep(args.interval)
        except KeyboardInterrupt:
            pass


def main():
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--host", help="MQTT-Broker-Adresse")
    source.add_argument("--serial", help="Serieller Port (z.B. COM3 oder /dev/ttyUSB0) statt MQTT")
    parser.add_argument("--port", type=int, default=1883, help="MQTT-Broker-Port")
    parser.add_argument("--topic", default="pulsemqtt/hwinfo", help="MQTT-Topic")
    parser.add_argument("--user", default=None, help="MQTT-Benutzername")
    parser.add_argument("--password", default=None, help="MQTT-Passwort")
    parser.add_argument("--interval", type=float, default=2.0, help="Sekunden zwischen Updates")
    args = parser.parse_args()

    if args.serial:
        run_serial(args)
    else:
        run_mqtt(args)


if __name__ == "__main__":
    main()