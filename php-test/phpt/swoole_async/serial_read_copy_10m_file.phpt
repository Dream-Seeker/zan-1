--TEST--
swoole_async: sequence copy 10m file

--SKIPIF--
<?php require  __DIR__ . "/../inc/skipif.inc"; ?>
--INI--
assert.active=1
assert.warning=1
assert.bail=0
assert.quiet_eval=0


--FILE--
<?php
require_once __DIR__ . "/../inc/zan.inc";
require_once __DIR__ . "/../../apitest/swoole_async/read_write.php";


serial_read_copy(10);
?>

--EXPECT--
SUCCESS

--CLEAN--