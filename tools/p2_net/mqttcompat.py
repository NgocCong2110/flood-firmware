"""Tạo client paho-mqtt chạy được với cả paho 1.x và 2.x. Cài: pip install paho-mqtt"""
import paho.mqtt.client as mqtt


def make_client(client_id: str) -> mqtt.Client:
    try:
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id=client_id)  # paho 2.x
    except AttributeError:
        return mqtt.Client(client_id=client_id)  # paho 1.x
