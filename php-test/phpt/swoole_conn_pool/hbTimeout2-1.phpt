--TEST--
swoole_conn_pool: h timeout 2 - 1

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
 * Date: 17/5/23
 * Time: 下午12:03
 */


$tcp_pool = new \swoole_connpool(\swoole_connpool::SWOOLE_CONNPOOL_TCP);
$r = $tcp_pool->setConfig([
    "host" => IP_BAIDU, // baidu
    "port" => 80,
    "hbIntervalTime" => 1,
    "hbTimeout" => 1,
]);
assert($r === true);

$tcp_pool->on("hbConstruct", function() {
    return [
        "method" => "send",
        "args" => "GET / HTTP 1.1\r\n\r\n\r\n",
    ];
});
$tcp_pool->on("hbCheck", function(\swoole_connpool $pool, $conn, $data) {
    assert(false);
    swoole_event_exit();
    return false;
});


$r = $tcp_pool->createConnPool(1, 1);
assert($r === true);

$timeout = 1000;

$timerId = swoole_timer_after($timeout + 100, function() use(&$got){
    assert(false);
    swoole_event_exit();
});

$connId = $tcp_pool->get($timeout, function(\swoole_connpool $pool, /*\swoole_client*/ $cli) use($timerId) {
    swoole_timer_clear($timerId);

    if ($cli instanceof \swoole_client) {
        assert($cli->isConnected());
        $pool->release($cli);
    } else {
        assert(false);
        swoole_event_exit();
    }


    swoole_timer_after(100, function() {
        // 这里应该优先于 心跳回调执行，因为心跳会超时 !
        swoole_event_exit();
        echo "SUCCESS";
    });
});
assert($connId !== false);

?>

--EXPECT--
SUCCESS