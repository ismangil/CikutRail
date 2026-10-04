#!/usr/bin/env python3
"""Drive a broker the way JMRI does, for repeatable turnout node tests.

JMRI publishes turnout commands retained, at QoS 2, on
<channel>track/turnout/<name> (docs/MQTT_CONVENTIONS.md). This tool does
the same, and can watch what the node publishes under cikutrail/<node>/.

Needs paho-mqtt (Debian/Raspberry Pi OS: sudo apt install python3-paho-mqtt,
or pip install paho-mqtt).

  mqtt_exercise.py set 101 CLOSED            one turnout, as JMRI would
  mqtt_exercise.py set 101-111 THROWN        several at once
  mqtt_exercise.py cycle 101 10 2000         toggle 10 times, 2 s apart
  mqtt_exercise.py payload 101 UNKNOWN       any payload, not retained
  mqtt_exercise.py clear 101-111             remove the retained commands
  mqtt_exercise.py node turnout1             the node's status and info
  mqtt_exercise.py watch                     print everything, Ctrl-C stops

Broker options go before the command: --host, --port, --user, --password
(or MQTT_PASSWORD in the environment), --channel (JMRI's channel, default
empty).
"""

import argparse
import os
import sys
import threading
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:
    sys.exit("needs paho-mqtt: sudo apt install python3-paho-mqtt (or pip install paho-mqtt)")

COMMAND_QOS = 2  # as JMRI publishes


def make_client(client_id):
    try:
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    except AttributeError:  # paho-mqtt 1.x
        return mqtt.Client(client_id=client_id)


def parse_names(text):
    """'101', '101,103' or '101-111' (numeric ranges) into a list of names."""
    names = []
    for part in text.split(","):
        if "-" in part and all(p.isdigit() for p in part.split("-", 1)):
            first, last = (int(p) for p in part.split("-", 1))
            if last < first:
                raise argparse.ArgumentTypeError(f"bad range {part}")
            names.extend(str(n) for n in range(first, last + 1))
        elif part:
            names.append(part)
    if not names:
        raise argparse.ArgumentTypeError("no turnout names")
    return names


def turnout_topic(args, name):
    return f"{args.channel}track/turnout/{name}"


class Broker:
    def __init__(self, args):
        self.client = make_client(f"mqtt_exercise-{os.getpid()}")
        if args.user:
            self.client.username_pw_set(args.user, args.password)
        self.client.connect(args.host, args.port, keepalive=30)
        self.client.loop_start()

    def publish(self, topic, payload, retain, qos=COMMAND_QOS):
        info = self.client.publish(topic, payload, qos=qos, retain=retain)
        info.wait_for_publish(timeout=10)
        if not info.is_published():
            sys.exit(f"publish to {topic} not acknowledged")

    def close(self):
        self.client.loop_stop()
        self.client.disconnect()


def stamp():
    return time.strftime("%H:%M:%S")


def cmd_set(args, broker):
    for name in args.names:
        broker.publish(turnout_topic(args, name), args.state, retain=True)
        print(f"{stamp()} {turnout_topic(args, name)} {args.state} (retained)")
        if args.gap_ms:
            time.sleep(args.gap_ms / 1000)


def cmd_cycle(args, broker):
    state = args.start
    for i in range(args.count):
        broker.publish(turnout_topic(args, args.name), state, retain=True)
        print(f"{stamp()} {i + 1}/{args.count} {turnout_topic(args, args.name)} {state}")
        state = "THROWN" if state == "CLOSED" else "CLOSED"
        if i + 1 < args.count:
            time.sleep(args.interval_ms / 1000)


def cmd_payload(args, broker):
    broker.publish(turnout_topic(args, args.name), args.text, retain=args.retain)
    print(f"{stamp()} {turnout_topic(args, args.name)} {args.text!r}{' (retained)' if args.retain else ''}")


def cmd_clear(args, broker):
    for name in args.names:
        broker.publish(turnout_topic(args, name), b"", retain=True)
        print(f"{stamp()} {turnout_topic(args, name)} retained command removed")


def print_message(message):
    payload = message.payload.decode(errors="replace")
    flag = " (retained)" if message.retain else ""
    print(f"{stamp()} {message.topic} {payload!r}{flag}", flush=True)


def cmd_node(args, broker):
    seen = {}
    done = threading.Event()
    wanted = {f"cikutrail/{args.node}/status", f"cikutrail/{args.node}/info"}

    def on_message(client, userdata, message):
        seen[message.topic] = message
        if wanted <= seen.keys():
            done.set()

    broker.client.on_message = on_message
    for topic in sorted(wanted):
        broker.client.subscribe(topic, qos=1)
    done.wait(args.wait)
    for topic in sorted(wanted):
        if topic in seen:
            print_message(seen[topic])
        else:
            print(f"{topic}: nothing retained")


def cmd_watch(args, broker):
    broker.client.on_message = lambda client, userdata, message: print_message(message)
    topics = [f"cikutrail/#", f"{args.channel}track/#"]
    for topic in topics:
        broker.client.subscribe(topic, qos=1)
    print(f"watching {', '.join(topics)}; Ctrl-C stops", flush=True)
    try:
        if args.seconds:
            time.sleep(args.seconds)
        else:
            while True:
                time.sleep(1)
    except KeyboardInterrupt:
        pass


def state_arg(text):
    state = text.upper()
    if state not in ("CLOSED", "THROWN"):
        raise argparse.ArgumentTypeError("state must be CLOSED or THROWN")
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--user", default=os.environ.get("MQTT_USER", ""))
    parser.add_argument("--password", default=os.environ.get("MQTT_PASSWORD", ""))
    parser.add_argument("--channel", default="", help="JMRI channel, e.g. /trains/ (default empty)")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("set", help="publish CLOSED/THROWN retained at QoS 2, as JMRI does")
    p.add_argument("names", type=parse_names)
    p.add_argument("state", type=state_arg)
    p.add_argument("--gap-ms", type=int, default=0, help="pause between turnouts")
    p.set_defaults(func=cmd_set)

    p = sub.add_parser("cycle", help="toggle one turnout")
    p.add_argument("name")
    p.add_argument("count", type=int)
    p.add_argument("interval_ms", type=int, help="keep above the GreenHat pulse length")
    p.add_argument("--start", type=state_arg, default="CLOSED")
    p.set_defaults(func=cmd_cycle)

    p = sub.add_parser("payload", help="publish any payload (not retained unless --retain)")
    p.add_argument("name")
    p.add_argument("text")
    p.add_argument("--retain", action="store_true")
    p.set_defaults(func=cmd_payload)

    p = sub.add_parser("clear", help="remove retained commands")
    p.add_argument("names", type=parse_names)
    p.set_defaults(func=cmd_clear)

    p = sub.add_parser("node", help="print the node's retained status and info")
    p.add_argument("node")
    p.add_argument("--wait", type=float, default=2.0)
    p.set_defaults(func=cmd_node)

    p = sub.add_parser("watch", help="print node and turnout messages")
    p.add_argument("--seconds", type=float, default=0, help="stop after this long (default: Ctrl-C)")
    p.set_defaults(func=cmd_watch)

    args = parser.parse_args()
    if getattr(args, "count", 1) < 1:
        parser.error("count must be at least 1")
    if getattr(args, "interval_ms", 250) < 250:
        parser.error("interval_ms below 250 would cut GreenHat pulses short")
    broker = Broker(args)
    try:
        args.func(args, broker)
    finally:
        broker.close()


if __name__ == "__main__":
    main()
