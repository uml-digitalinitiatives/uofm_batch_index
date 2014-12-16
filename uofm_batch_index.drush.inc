<?php


function uofm_batch_index_drush_command() {
  $items = array();

  $items['uofm_batch_index_process'] = array(
    'options' => array(
      
      'timeout' => array(
        'description' => 'Length of time (in secs) to run before exiting, if omitted run until complete.',
        'value' => 'optional',
      ),
    ),
    'aliases' => array('uofm_batch_index'),
    'callback' => 'uofm_batch_index_setup',
    'description' => 'Batch index objects missed by FedoraGSearch based on a Sparql query where clause',
    'drupal dependencies' => array('islandora', 'islandora_solr', 'islandora_collection_search'),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN,
  );
  
  $items['uofm_batch_index_preprocess'] = array(
    'options' => array(
      'query' => array(
        'description' => 'The SPARQL where clause, uses "$object" as the main variable.',
      ),
      'pid' => array(
        'description' => 'A PID to re-index',
      ),
      'pidlist' => array(
        'description' => 'A comma seperated list of PIDs to re-index',
      ),
      'pidfile' => array(
        'description' => 'Path to a textfile of PIDs to re-index, one per line',
      ),
      'force' => array(
        'description' => 'Force indexing even if already in Solr, CAREFUL this could re-index ALOT of stuff.',
        'value' => 'optional',
      ),
    ),
    'aliases' => array('uofm_bi_pp'),
    'callback' => 'uofm_batch_index_preprocess_queue',
    'description' => 'Add items to the queue to be re-indexed on cron runs.',
    'drupal dependencies' => array('islandora', 'islandora_solr', 'islandora_collection_search'),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN,
  );

  return $items;
}

function uofm_batch_index_setup(){
  $batch_size = 20; // Number of records to process before we check the time again.
  
  $timeout = drush_get_option('timeout',3600.0);
  if (!isset($timeout) || $timeout < 0) {
    // XXX: Magic value (negative), causes the batch to run until all objects
    // have been processed.
    $timeout = -1.0;

    $max_exec = intval(ini_get('max_execution_time'));
    $month = ((3600 * 24) * 30); // a month is a long time
    if ($max_exec > 0) {
      // max_execution_time could be 0 if run from CLI (drush?)
      $lock_timeout = min($month, $max_exec);
    }
    else {
      // Lock for a month, if we are running with no timeout.
      $lock_timeout = $month;
    }
  }
  else {
    $lock_timeout = min(86400.0, $timeout); // No more than 24 hours at a time.
  }
  
  $timer = timer_start('uofm_batch_index');
  
  $query = db_select('uofm_batch_index_state','u')->condition('u.state',0)->fields('u',array('pid'));
  $num_ops = $query->countQuery()->execute()->fetchField();
  $after_ops = $num_ops;
  
  if ($num_ops > 0){
    drush_log("There are $num_ops items to be indexed.",'notice');
    drush_log("timer (" . timer_read('uofm_batch_index') . "), lock * 1000 (" . ($lock_timeout * 1000) . ")", "notice");
    $loop_count = 0;
    while (timer_read('uofm_batch_index') < ($lock_timeout * 1000) && $after_ops > 0){
      drush_log("range $loop_count, $batch_size");
      $results = $query->range($loop_count,$batch_size)->execute()->fetchAll(PDO::FETCH_NUM);
      $operations = array();
      array_walk($results,'uofm_batch_index_op_array', $operations);
      
      $batch = array(
         'operations' => $operations,
         //'finished' => 'drush_uofm_batch_index_finished',
         'title' => t('Indexing missing object from repository'),
         'init_message' => t('Batch is starting.'),
         'progress_message' => t('Processed @current out of @total.'),
         'error_message' => t('Batch has encountered an error.'),
         'file' => drupal_get_path('module', 'uofm_batch_index') . '/uofm_batch_index.drush.inc',
      );
      batch_set($batch);
      $batch =& batch_get();
      $batch['progressive'] = FALSE;
      drush_backend_batch_process();
      #$loop_count += 1;
      $after_ops = $query->countQuery()->execute()->fetchField();
    }
    $processed = $num_ops - $after_ops;
    $timer = timer_stop('uofm_batch_index');
    $time = $timer['time'] / 1000;
    $format = "d \d\a\y\s h:i:s";
    watchdog('uofm_batch_index','Finished indexing %p records in %t, %l remaining',array('%p'=>$processed,'%t'=>date($format, $time), '%l' => $after_ops ),WATCHDOG_INFO);
  } else {
    timer_stop('uofm_batch_index');
    watchdog('uofm_batch_index','There are NO objects to be indexed',NULL, WATCHDOG_INFO);
  }
}

function uofm_batch_index_op_array($val,$key,&$out_array){
  foreach ($val as $k => $v){
    $out_array[] = array('drush_uofm_batch_index_doot', array($v));
  }
}
function drush_uofm_batch_index($object, &$context){
  $context['message'] = "Processing object $object";
  drush_log("processing ($object)","notice");
  $url = variable_get('islandora_collection_search_gsearch_endpoint','http://localhost:8080/fedoragsearch/rest');
  $user = variable_get('islandora_collection_search_gsearch_user',NULL);
  $passwd = variable_get('islandora_collection_search_gsearch_password',NULL);
  $ch = curl_init();
  curl_setopt($ch, CURLOPT_URL,  $url . "?" . http_build_query(array("operation"=>"updateIndex","action"=>"fromPid", "value"=> $object)));
  curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
  curl_setopt($ch, CURLOPT_HEADER, 0);
  curl_setopt($ch, CURLOPT_USERPWD, "$user:$passwd");
  $res = curl_exec($ch);
  curl_close($ch);
  $time = (int)time();
  if ($res === FALSE) {
    drush_log('Failed to index PID (' . $object . ')', "error");
  } else {
    try {
       db_update('uofm_batch_index_state')->fields(array('state' => 2, 'idx_time' => $time))->condition('pid',$object)->execute();
    } catch (Exception $e) {
      watchdog_exception('uofm_batch_index',$e);
    }
  }   
}

function drush_uofm_batch_index_finished($success, $results, $operations){
  // Print finished message to user.
  drush_log('Finished updating compound object sequence numbers.','ok');
}

function uofm_batch_index_preprocess_queue() {
  if (is_null(drush_get_option('query', NULL)) && is_null(drush_get_option('pid', NULL)) && is_null(drush_get_option("pidlist", NULL)) && is_null(drush_get_option("pidfile", NULL))) {
    drush_set_error('no value', 'You must enter one of --query, --pid, --pidlist or --pidfile');
  }
  $user = variable_get('islandora_collection_search_gsearch_user',NULL);
  $passwd = variable_get('islandora_collection_search_gsearch_password',NULL);
  if (is_null($user) || is_null($passwd)){
    drush_log('GSearch username or passwd has not been set, check the Collection Search admin page.','error');
    return;
  }
  module_load_include('inc', 'islandora', 'includes/utilities');
  module_load_include('php','islandora_solr','SolrPhpClient/Apache/Solr/Service');

  $path_parts = parse_url(variable_get('islandora_solr_url', 'localhost:8080/solr')); 
  $solr = new Apache_Solr_Service($path_parts['host'], $path_parts['port'], $path_parts['path'] . '/');
  $solr->setCreateDocuments(0);

  $results = array();
  $force = (bool) drush_get_option('force', FALSE);
  if (!is_null(drush_get_option('query', NULL))) { 
    drupal_static_reset('islandora_get_tuque_connection');
    $connection = islandora_get_tuque_connection();

    $where_param = drush_get_option('query', FALSE);
  
  
    $query = 'select $object from <#ri> where {';
    $query.= $where_param;
    $query.= '}';

    $results = $connection->repository->ri->sparqlQuery($query, 'unlimited');
  }
  else if (!is_null(drush_get_option('pid', NULL))) {
    $results[] = drush_get_option('pid', "");
  }
  else if (!is_null(drush_get_option('pidlist', NULL))) {
    $list = drush_get_option('pidlist', "");
    $tmpRes = explode($list, ',');
    foreach ($tmpRes as $t) {
      if (strlen(trim($t)) > 0) {
        $results[] = trim($t);
      }
    }
  }
  else if (!is_null(drush_get_option('pidfile', NULL))) {
    if (file_exists(drush_get_option('pidfile', ''))) {
      $fp = @fopen(drush_get_option('pidfile', ''), 'r');
      if (!$fp) {
        drush_set_error("Unable to open file " . drush_get_option('pidfile', ''));
      }
      while (!feof($fp)) {
        $line = trim(fgets($fp, 4096));
        drush_log("Processing line ($line)", "notice");
        if (is_array($line)) {
          ob_start();
          var_dump($line);
          $x = ob_get_clean();
          drush_set_error("Trying to process an array ($x)");
        }
        if (!empty($line)) {
          $results[] = trim($line);
        }
      }
    }
    else {
      drush_set_error("File: " . drush_get_option('pidfile', '') . " does not exist.");
    }
  }
  $operations = array();
  $queue = DrupalQueue::get('uofm_batch_reindex');
  foreach ($results as $object) {
    if (is_array($object) && array_key_exists('object', $object)) {
      $object = $object['object']['value'];
    }
    $solr_query = '+PID:("' . $object . '")'; // Search for all the possible PIDs
    $solr_params = array('fl'=>'PID'); // Get the PID and dc.title
    $results = $solr->search($solr_query,0,10,$solr_params);
    if ($results && ($results->response->numFound <= 0 || $force)){ // Only add if not in Solr index
      $queue->createItem($object);
    }
  }
}


function uofm_batch_index_preprocess(){
  uofm_batch_index_cleanup(); // Delete old records so the table doesn't get huge.
  $user = variable_get('islandora_collection_search_gsearch_user',NULL);
  $passwd = variable_get('islandora_collection_search_gsearch_password',NULL);
  if (is_null($user) || is_null($passwd)){
    drush_log('GSearch username or passwd has not been set, check the Collection Search admin page.','error');
    return;
  }
  module_load_include('inc', 'islandora', 'includes/utilities');
  module_load_include('php','islandora_solr','SolrPhpClient/Apache/Solr/Service');

  $path_parts = parse_url(variable_get('islandora_solr_url', 'localhost:8080/solr')); 
  $solr = new Apache_Solr_Service($path_parts['host'], $path_parts['port'], $path_parts['path'] . '/');
  $solr->setCreateDocuments(0);

  drupal_static_reset('islandora_get_tuque_connection');
  $connection = islandora_get_tuque_connection();

  $where_param = drush_get_option('query_param', FALSE);
  $force = (bool) drush_get_option('force', FALSE);
  
  
  $query = 'select $object from <#ri> where {';
  $query.= $where_param;
  $query.= '}';
dd($query,'query');
  $results = $connection->repository->ri->sparqlQuery($query, 'unlimited');
  $operations = array();
  foreach ($results as $object) {
    $solr_query = '+PID:("' . $object['object']['value'] . '")'; // Search for all the possible PIDs
    $solr_params = array('fl'=>'PID'); // Get the PID and dc.title
    $results = $solr->search($solr_query,0,10,$solr_params);
    if ($results && ($results->response->numFound <= 0 || $force)){ // Only add if not in Solr index
      try {
        $nid = db_insert('uofm_batch_index_state')->fields(array(
          'pid' => $object['object']['value'],
        ))->execute();
      } catch (Exception $e){
        drush_log('Unable to insert pid (' . $object['object']['value'] . ') already exists','warning');
      }
    }
  }
}

function uofm_batch_index_cleanup(){
  // This should remove items indexed over 2 weeks ago.
  $time = REQUEST_TIME;
  $time = $time - ((3600 * 24) * 14); // subtract 2 weeks
  try {
    db_delete('uofm_batch_index_state')->isNotNull('idx_time')->condition('idx_time',$time,'<')->condition('state','2');
  } catch (Exception $e){
    watchdog_exception('error',$e);
  }
}
