"""Passive production-domain check; never publishes samples or opens hardware."""
import json
import os
from pathlib import Path
import time

os.environ['ROS_DOMAIN_ID'] = '42'
import rclpy
import robot_interfaces.msg as messages
from robot_interfaces.msg import ThicknessReading, ThicknessStatus

rclpy.init()
node = rclpy.create_node('measurement_simplification_observer')
readings = []
processed_data = []
statuses = []
node.create_subscription(ThicknessReading, '/thickness/reading', readings.append, 64)
node.create_subscription(ThicknessReading, '/measurement/thickness', processed_data.append, 64)
node.create_subscription(ThicknessStatus, '/thickness/status', statuses.append, 1)
deadline = time.monotonic() + 6
while time.monotonic() < deadline:
    rclpy.spin_once(node, timeout_sec=.1)
services = dict(node.get_service_names_and_types())
topics = dict(node.get_topic_names_and_types())
subscriptions = node.get_subscriber_names_and_types_by_node('measurement_manager_node', '/')
assert '/measurement/set_enabled' not in services
assert '/measurement/control' not in topics
assert not hasattr(messages, 'MeasurementResult')
assert not hasattr(messages, 'MeasurementControl')
business_subscriptions = {name for name, _ in subscriptions
                          if name.startswith(('/thickness/', '/measurement/'))}
assert business_subscriptions == {'/thickness/reading'}
raw_sequences = {reading.sequence for reading in readings}
report = dict(raw_readings=len(readings), processed_data=len(processed_data),
              matched_data=sum(data.sequence in raw_sequences for data in processed_data),
              manager_subscriptions=subscriptions, obsolete_interfaces_absent=True,
              latest_value_um=processed_data[-1].value if processed_data else None)
if statuses:
    status = statuses[-1]
    report['instrument'] = dict(port_open=status.port_open,
                               communication_ready=status.communication_ready,
                               valid_frames=status.valid_frames,
                               io_errors=status.io_errors, last_error=status.last_error)
output = Path('/workspace/test/measurement_manager/results/runtime_verification.json')
output.write_text(json.dumps(report, ensure_ascii=False, indent=2))
print(json.dumps(report, ensure_ascii=False, indent=2))
node.destroy_node()
rclpy.shutdown()
