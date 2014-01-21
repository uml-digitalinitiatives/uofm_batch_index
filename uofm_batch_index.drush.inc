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
      'query_param' => array(
        'description' => 'The SPARQL where clause, uses "$object" as the main variable.',
        'value' => 'required',
        'required' => TRUE,
      ),
    ),
    'aliases' => array('uofm_bi_pp'),
    'callback' => 'uofm_batch_index_preprocess',
    'description' => 'Run the query against the repository and store the IDs of all none indexed documents to be processed.',
    'drupal dependencies' => array('islandora', 'islandora_solr', 'islandora_collection_search'),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN,
  );

  return $items;
}

function uofm_batch_index_setup(){
  $batch_size = 20; // Number of records to process before we check the time again.
  
  $timeout = drush_get_option('timeout',3600.0);
  if (!isset($timeout)) {
    // XXX: Magic value (negative), causes the batch to run until all objects
    // have been processed.
    $timeout = -1.0;

    $max_exec = intval(ini_get('max_execution_time'));
    if ($max_exec > 0) {
      // max_execution_time could be 0 if run from CLI (drush?)
      $lock_timeout = min(3600.0, $max_exec);
    }
    else {
      // Lock for an hour by default.
      $lock_timeout = 3600.0;
    }
  }
  else {
    $lock_timeout = min(3600.0, $timeout);
  }
  
  $timer = timer_start('uofm_batch_index');
  
  $query = db_select('uofm_batch_index_state','u')->condition('u.status',0)->fields('u',array('pid'));
  $num_ops = $query->countQuery()->execute()->fetchField();
  
  if ($num_ops > 0){
    drush_log("There are $num_ops items to be indexed.",'ok');
    $loop_count = 0;
    while (timer_read('uofm_batch_index') < ($lock_timeout * 1000) && ($loop_count * $batch_size) + $batch_size < $num_ops ){
      $results = $query->range($loop_count,$batch_size)->execute()->fetchAll();
      
      $batch = array(
         'operations' => $results,
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
    }
  } else {
    drush_log('There are NO objects to be indexed','ok');
  }
  timer_stop('uofm_batch_index');
}

function drush_uofm_batch_index($object){
   drush_log('Forcing index of object: ' . $object, 'ok');
   $url = variable_get('islandora_collection_search_gsearch_endpoint','http://localhost:8080/fedoragsearch/rest');
   $user = variable_get('islandora_collection_search_gsearch_user',NULL);
   $passwd = variable_get('islandora_collection_search_gsearch_password',NULL);
   $ch = curl_init();
   curl_setopt($ch, CURLOPT_URL,  $url . "?" . http_build_query(array("operation"=>"updateIndex","action"=>"fromPid", "value"=> $object)));
   curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
   curl_setopt($ch, CURLOPT_HEADER, 0);
   curl_setopt($ch, CURLOPT_USERPWD, "$user:$passwd");
   curl_exec($ch);
   curl_close($ch);
   db_update('uofm_batch_index_state')->fields(array('state' => 2))->condition('pid',$object);
}

function drush_uofm_batch_index_finished($success, $results, $operations){
  // Print finished message to user.
  drush_log('Finished updating compound object sequence numbers.','ok');
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
  
  $query = 'select $object from <#ri> where {';
  $query.= $where_param;
  $query.= '}';
  $results = $connection->repository->ri->sparqlQuery($query, 'unlimited');
  $operations = array();
  foreach ($results as $object) {
    $solr_query = '+PID:("' . $object['object']['value'] . '")'; // Search for all the possible PIDs
    $solr_params = array('fl'=>'PID'); // Get the PID and dc.title
    $results = $solr->search($solr_query,0,10,$solr_params);
    if ($results && $results->response->numFound <= 0){ // Only add if not in Solr index
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
    db_delete('uofm_batch_index_state')->isNotNull('idx_time')->condition('idx_time',$time,'<')->condition('status','2');
  } catch (Exception $e){
    watchdog_exception('error',$e);
  }
}