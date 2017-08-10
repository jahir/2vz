<?php

# read config
require '.luftdaten.conf.php';
$required = array('spool', 'sensor');
foreach ($required as $s) {
	if (!array_key_exists($s, $cfg)) {
		error_log("option '$s' not set in config");
		exit;
	}
}

$vz_spool = $cfg['spool'];
$sensor = $cfg['sensor'];

# process data
$json = file_get_contents('php://input');

$o = json_decode($json, TRUE);
if (is_null($o)) {
	error_log("bad json in '$json': " . json_last_error_msg());
	exit;
}

if (!array_key_exists('sensordatavalues', $o)) {
	error_log("missing sensordatavalues in '$json'");
	exit;
}

$ts = time();
foreach ($o["sensordatavalues"] as $v) {
	$key = $v['value_type'];
	$val = $v['value'];
	if (array_key_exists($key, $sensor)) {
		$path = sprintf('%s/%u000_%s_%s', $vz_spool, $ts, $sensor[$key], $val);
		if (touch($path) === FALSE)
			error_log("error writing to $path");
	}
}

?>
