--TEST--
swoole_conn_pool: get 2 - 2

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
 * Time: 下午8:56
 */


$redis_pool = new \swoole_connpool(\swoole_connpool::SWOOLE_CONNPOOL_REDIS);
$r = $redis_pool->setConfig([
    "host" => "11.11.11.11",
    "port" => REDIS_SERVER_PORT,
]);
assert($r === true);
$r = $redis_pool->createConnPool(1, 1);
assert($r === true);

$timeout = 100;

$timerId = swoole_timer_after($timeout + 100, function() {
    assert(false);
    swoole_event_exit();
});

// 已经FIX timeout 参数有问题
$connId = $redis_pool->get(100, function(\swoole_connpool $pool, /*\swoole_client*/$cli) use($timerId) {
    swoole_timer_clear($timerId);
    if ($cli instanceof \swoole_redis) {
        assert(false);
    } else {
        echo "TIMEOUT";
        assert(true);
    }
    swoole_event_exit();
});
if ($connId === false) {
    assert(false);
    swoole_event_exit();
}

?>

--EXPECT--
TIMEOUT
