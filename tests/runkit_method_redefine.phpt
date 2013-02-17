--TEST--
runkit_method_redefine() function
--SKIPIF--
<?php if(!extension_loaded("runkit") || !RUNKIT_FEATURE_MANIPULATION) print "skip"; ?>
--INI--
error_reporting=E_ALL
display_errors=on
--FILE--
<?php
class runkit_class {
	function runkit_method($a) {
		echo "a is $a\n";
	}
}

if (version_compare(phpversion(), "5.4.0") >= 0) {
  /* Ignore ZEND_ACC_ALLOW_STATIC strict notice */
  error_reporting(E_ALL & ~E_STRICT);
}

runkit_class::runkit_method('foo');

error_reporting(E_ALL);

runkit_method_redefine('runkit_class','runkit_method',
                       '$b', 'echo "b is $b\n";',
                       RUNKIT_ACC_PUBLIC | RUNKIT_ACC_STATIC);
runkit_class::runkit_method('bar');
?>
--EXPECT--
a is foo
b is bar
