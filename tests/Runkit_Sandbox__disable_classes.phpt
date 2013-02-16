--TEST--
Runkit_Sandbox() - disable_classes test
--SKIPIF--
<?php if(!extension_loaded("runkit") || !RUNKIT_FEATURE_SANDBOX) print "skip"; 
      /* May not be available due to lack of TSRM interpreter support */
      if(!function_exists("runkit_lint")) print "skip"; ?>
--FILE--
<?php
function output_handler($msg) {
  if (strncmp(trim($msg), 'Warning: ', 9) == 0) echo "True";
}

$php = new Runkit_Sandbox(array('disable_classes'=>'stdClass'));
$php['output_handler'] = 'output_handler';
$php->ini_set('html_errors',false);
$php->eval('$a = new stdClass();');
--EXPECT--
True
