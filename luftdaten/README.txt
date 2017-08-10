
http://luftdaten.info/ describes how to build your own particulate matter
sensor, powered by a NodeMCU (ESP8266). The firmware is able to push the
data not only to luftdaten.info, but also to a custom http URL. You will
get a POST request with a JSON like this:

{"esp8266id": "xxxxxxxx", "software_version": "NRZ-2017-092", "sensordatavalues":[{"value_type":"SDS_P1","value":"1.80"},{"value_type":"SDS_P2","value":"1.70"},{"value_type":"temperature","value":"25.90"},{"value_type":"humidity","value":"50.00"},{"value_type":"samples","value":"755761"},{"value_type":"min_micro","value":"188"},{"value_type":"max_micro","value":"26531"},{"value_type":"signal","value":"-36"}]}

luftdaten.php reads this JSON an writes a spool file for vzspool.

Config is in luftdaten.conf.php, which must be adjusted (esp. the UUIDs) and
copied to .luftdaten.conf.php (your webserver should block requests for files
that start with a dot, just for safety, even though the data is not really
private).
