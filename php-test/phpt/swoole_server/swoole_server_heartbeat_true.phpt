--TEST--
swoole_server: heart beat (true)
--SKIPIF--
<?php require __DIR__ . "/../inc/skipif.inc"; ?>
--INI--
assert.active=1
assert.warning=1
assert.bail=0
assert.quiet_eval=0


--FILE--
<?php
/**
 * Created by IntelliJ IDEA.
 * User: chuxiaofeng
 * Date: 17/6/7
 * Time: 下午4:34
 */
require_once __DIR__ . "/../inc/zan.inc";

$simple_tcp_server = __DIR__ . "/../../apitest/swoole_server/opcode_server.php";
$port = get_one_free_port();

start_server($simple_tcp_server, TCP_SERVER_HOST, $port);

suicide(2000);
usleep(500 * 1000);

makeTcpClient(TCP_SERVER_HOST, $port, function(\swoole_client $cli) {
    $r = $cli->send(opcode_encode("heartbeat", [true]));
    assert($r !== false);
}, function(\swoole_client $cli, $recv) {
    list($op, $data) = opcode_decode($recv);
    // TODO
//    var_dump($data);

    swoole_event_exit();
    echo "SUCCESS";
});

?>
--EXPECT--
SUCCESS