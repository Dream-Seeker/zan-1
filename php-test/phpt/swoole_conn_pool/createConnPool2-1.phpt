--TEST--
swoole_conn_pool: create conn pool 2 - 1

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

/**
 * Created by IntelliJ IDEA.
 * User: chuxiaofeng
 * Date: 17/5/22
 * Time: 下午9:15
 */


require_once __DIR__ . "/connSizeTest.php";

// 测试连接池数量

$tcpConf = [
    "host" => IP_BAIDU, // baidu
    "port" => 80,
];

$tcpTest = makeConnSizeTest(\swoole_connpool::SWOOLE_CONNPOOL_TCP, $tcpConf);
$tcpTest();



?>

--EXPECT--
SUCCESS