<?php


function uofm_batch_index_drush_command() {
  $items = array();

  $items['uofm-batch-index'] = array(
    'options' => array(
      'query_param' => array(
        'description' => 'The SPARQL where clause.',
        'value' => 'required',
      ),
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN,
  );

  return $items;
}

function drush_uofm_batch_index_setup(){
  $user = variable_get('islandora_collection_search_gsearch_user',NULL);
  $passwd = variable_get('islandora_collection_search_gsearch_password',NULL);
  if (is_null($user) || is_null($passwd)){
    drupal_log('GSearch username or passwd has not been set, check the Collection Search admin page.','error');
    return;
  }
  module_load_include('php','islandora_solr','SolrPhpClient/Apache/Solr/Service');
  $path_parts = parse_url(variable_get('islandora_solr_url', 'localhost:8080/solr')); 
  #$path_parts = parse_url('digitalcollections.lib.umanitoba.ca:8080/solr');
  $solr = new Apache_Solr_Service($path_parts['host'], $path_parts['port'], $path_parts['path'] . '/');

  $solr->setCreateDocuments(0);
  
  $query = drush_get_option('query_param', FALSE);
  $t = 'uofm_batch_index_timeout';
  $timeout = drush_get_option('timeout', NULL);
  timer_start($t);

  $ops = array(
    'timeout' => $timeout,
  );

  drupal_static_reset('islandora_get_tuque_connection');
  $connection = islandora_get_tuque_connection();
  if ($pid !== FALSE){
    $results[]['object']['value'] = $pid;
  } else {
    $query = 'select $object $label from <#ri> where {';
    $query.= $query;
    $query.= '}';
    $results = $connection->repository->ri->sparqlQuery($query, 'unlimited');
  }
  $operations = array();
  foreach ($results as $object) {
    $solr_query = '+PID:("' . $object['object']['value'] . '")'; // Search for all the possible PIDs
    $solr_params = array('fl'=>'PID'); // Get the PID and dc.title
    $results = $solr->search($solr_query,0,10,$solr_params);
    if ($numfound = $results->get('numFound') && $numfound <= 0){ // Only add if not in Solr index
      array_push($operations, array("drush_uofm_batch_index",
        array($object['object']['value']),
        )
      );
      
    }
  }
  
  dd($operations,'To be indexed');
  exit;
  
  $batch = array(
     'operations' => $operations,
     'finished' => 'drush_uofm_batch_index_finished',
     'title' => t('Recreating Sequence Numbers for Compound Images'),
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

function uofm_batch_index($object, &$context){
   $context['message'] = 'Forcing index of object: ' . $object;
   $url = variable_get('islandora_collection_search_gsearch_endpoint','http://localhost:8080/fedoragsearch/rest');
   $user = variable_get('islandora_collection_search_gsearch_user',NULL);
   $passwd = variable_get('islandora_collection_search_gsearch_password',NULL);
   $ch = curl_init();
   curl_setopt($ch, CURLOPT_URL,  $url . "?" . http_build_query(array("operation"=>"updateIndex","action"=>"updateFromPid", "pid"=> $object)));
   curl_setopt($ch, CURLOPT_HEADER, 0);
   curl_setopt($ch, CURLOPT_USERPWD, "$user:$passwd");
   curl_exec($ch);
   curl_close($ch);
}

function drush_uofm_batch_index_finished($success, $results, $operations){
  // Print finished message to user.
  drush_log('Finished updating compound object sequence numbers.','ok');
}