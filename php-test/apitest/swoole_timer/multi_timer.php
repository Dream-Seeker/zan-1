<?php

require_once __DIR__ . "/../../Bootstrap.php";

for ($j = 0; $j < 100; $j++) {
    swoole_timer_after(1, function() use($j){
        echo $j, "\n";
    });
}