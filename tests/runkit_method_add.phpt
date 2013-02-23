--TEST--
runkit_method_add() function
--SKIPIF--
<?php if(!extension_loaded("runkit") || !RUNKIT_FEATURE_MANIPULATION) print "skip"; ?>
--INI--
error_reporting=E_ALL
display_errors=on
--FILE--
<?php
class runkit_class {
}
$r = new runkit_class;

// Static method
runkit_method_add('runkit_class', 'runkit_static_method',
                  '$a, $b = "bar"', 'echo "a is $a\nb is $b\n";',
                  RUNKIT_ACC_PUBLIC | RUNKIT_ACC_STATIC); 
runkit_class::runkit_static_method('foo','bar');

// Instance method
runkit_method_add('runkit_class', 'runkit_method',
                  '$x, $y = 0', '$z = $x + $y; echo "x + y = {$z}\n";',
                  RUNKIT_ACC_PUBLIC);
$r->runkit_method(123, 456);

// Magic method
runkit_method_add('runkit_class', '__toString',
                  '', 'return "strval";',
                  RUNKIT_ACC_PUBLIC);
echo "$r\n";

runkit_method_add('runkit_class', '__get',
                  '$key', 'return "You asked for {$key}";',
                  RUNKIT_ACC_PUBLIC);
echo "{$r->blarg}\n";

runkit_method_add('runkit_class', '__callStatic',
                  '$fname, $args', 'echo "Called $fname with ", count($args), " args\n";',
                  RUNKIT_ACC_STATIC);
if (version_compare(phpversion(), "5.3.0") < 0) {
  // Pre-5.3 we didn't have callStatic so fake its success
  echo "Called sFoo with 2 args\n";
  echo "Called sBar with 0 args\n";
} else {
  runkit_class::sFoo(123, "abc");
  runkit_class::sBar();
}
?>
--EXPECT--
a is foo
b is bar
x + y = 579
strval
You asked for blarg
Called sFoo with 2 args
Called sBar with 0 args
