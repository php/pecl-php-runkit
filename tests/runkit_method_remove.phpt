--TEST--
runkit_method_remove() function
--SKIPIF--
<?php if(!extension_loaded("runkit") || !RUNKIT_FEATURE_MANIPULATION) print "skip"; ?>
--INI--
error_reporting=E_ALL
display_errors=on
--FILE--
<?php
class runkit_class {
	function runkit_method() {
		echo "Runkit Method\n";
	}
}

if (version_compare(phpversion(), "5.4.0") >= 0) {
  error_reporting(E_ALL & ~E_STRICT);
}

runkit_class::runkit_method();
runkit_method_remove('runkit_class','runkit_method');
if (!method_exists('runkit_class','runkit_method')) {
	echo "Runkit Method Removed\n";
}
?>
--EXPECT--
Runkit Method
Runkit Method Removed
