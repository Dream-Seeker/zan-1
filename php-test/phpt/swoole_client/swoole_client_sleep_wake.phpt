--TEST--
swoole_client: swoole_client sleep & sleep

--SKIPIF--
<?php require  __DIR__ . "/../inc/skipif.inc"; ?>
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
 * Time: 上午10:59
 */

require_once __DIR__ . "/../inc/zan.inc";

$simple_tcp_server = __DIR__ . "/../../apitest/swoole_server/simple_server.php";
start_server($simple_tcp_server, TCP_SERVER_HOST, TCP_SERVER_PORT);


suicide(5000);


$cli = new \swoole_client(SWOOLE_SOCK_TCP, SWOOLE_SOCK_ASYNC);


$cli->on("connect", function(swoole_client $cli) {
    swoole_timer_clear($cli->timeo_id);
    assert($cli->isConnected() === true);

    $r = $cli->sleep();
    assert($r);
    swoole_timer_after(200, function() use($cli) {
        $r = $cli->wakeup();
        assert($r);
    });
    $cli->send(RandStr::gen(1024, RandStr::ALL));
});

$cli->on("receive", function(swoole_client $cli, $data){
    $recv_len = strlen($data);
    $cli->send(RandStr::gen(1024, RandStr::ALL));
    $cli->close();
    assert($cli->isConnected() === false);
});

$cli->on("error", function(swoole_client $cli) {
    echo "error";
});

$cli->on("close", function(swoole_client $cli) {
    swoole_event_exit();
    echo "SUCCESS";
});

$cli->connect(TCP_SERVER_HOST, TCP_SERVER_PORT);

$cli->timeo_id = swoole_timer_after(1000, function() use($cli) {
    echo "connect timeout";
    $cli->close();
    assert($cli->isConnected() === false);
});

?>

--EXPECT--
SUCCESS