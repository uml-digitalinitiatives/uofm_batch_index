<?php
/**
 * @file
 * Drush commands to add items
 * to the reindexing queue.
 */

/**
 * Implements hook_drush_command().
 */
function uofm_batch_index_drush_command() {
  $items = array();

  $items['uofm_batch_index_preprocess'] = array(
    'options' => array(
      'query' => array(
        'description' => 'The SPARQL where clause, uses "?object" as the returned variable.',
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
      'recursive' => array(
        'description' => 'Re-index any children objects of those listed/queried',
        'value' => 'optional',
      ),
      'force' => array(
        'description' => 'Force indexing even if already in Solr, CAREFUL this could re-index ALOT of stuff.',
        'value' => 'optional',
      ),
    ),
    'aliases' => array('uofm_bi_pp'),
    'callback' => 'uofm_batch_index_preprocess_queue',
    'description' => 'Add items to the queue to be re-indexed on cron runs.',
    'drupal dependencies' => array(
      'islandora',
      'islandora_solr',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN,
  );

  return $items;
}

/**
 * Load pids into the queue.
 */
function uofm_batch_index_preprocess_queue() {
  if (is_null(drush_get_option('query', NULL)) &&
    is_null(drush_get_option('pid', NULL)) &&
    is_null(drush_get_option("pidlist", NULL)) &&
    is_null(drush_get_option("pidfile", NULL))) {
    drush_set_error('no value', 'You must enter one of --query, --pid, --pidlist or --pidfile');
  }
  $user = variable_get('islandora_collection_search_gsearch_user', NULL);
  $passwd = variable_get('islandora_collection_search_gsearch_password', NULL);
  if (is_null($user) || is_null($passwd)) {
    drush_log('GSearch username or passwd has not been set, check the Collection Search admin page.', 'error');
    return;
  }

  $results = array();
  $force = (bool) (!is_null(drush_get_option('force', NULL)));
  $recursive = (bool) (!is_null(drush_get_option('recursive', NULL)));

  if (!is_null(drush_get_option('query', NULL))) {
    $query_param = drush_get_option('query');
    $results = __uofm_batch_index_sparql_query($query_param);
  }
  elseif (!is_null(drush_get_option('pid', NULL))) {
    $pid = drush_get_option('pid', "");
    $results[] = $pid;
  }
  elseif (!is_null(drush_get_option('pidlist', NULL))) {
    $list = drush_get_option('pidlist', "");
    $tmp_res = explode($list, ',');
    foreach ($tmp_res as $t) {
      $t = trim($t);
      if (strlen($t) > 0) {
        $results[] = $t;
      }
    }
  }
  elseif (!is_null(drush_get_option('pidfile', NULL))) {
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
          $results[] = $line;
        }
      }
    }
    else {
      drush_set_error("File: " . drush_get_option('pidfile', '') . " does not exist.");
    }
  }

  if ($recursive) {
    $new_results = __uofm_batch_index_recursive_query($results);
    $results = array_unique($new_results);
  }
  __uofm_batch_index_add_to_queue($results, $force);

}

/**
 * Recurse down to the children.
 */
function __uofm_batch_index_recursive_query($pids, $results = array()) {
  foreach ($pids as $parent) {
    $results[] = $parent;
    $children = __uofm_batch_index_get_children($parent);
    if (is_array($children) && count($children) > 0) {
      $results = __uofm_batch_index_recursive_query($children, $results);
    }
  }
  return $results;
}

/**
 * Add an array of items to the Drupal queue.
 */
function __uofm_batch_index_add_to_queue(array $items, $force = FALSE) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  module_load_include('php', 'islandora_solr', 'SolrPhpClient/Apache/Solr/Service');

  $queue = DrupalQueue::get(UOFM_BATCH_INDEX_QUEUE);
  $path_parts = parse_url(variable_get('islandora_solr_url', 'localhost:8080/solr'));
  $solr = new Apache_Solr_Service($path_parts['host'], $path_parts['port'], $path_parts['path'] . '/');
  $solr->setCreateDocuments(0);

  foreach ($items as $object) {
    if (is_string($object) && islandora_is_valid_pid($object)) {
      $add = FALSE;
      if ($force) {
        $add = TRUE;
      }
      else {
        // Search for all the possible PIDs.
        $solr_query = '+PID:("' . $object . '")';
        $solr_params = array('fl' => 'PID');
        $res = $solr->search($solr_query, 0, 10, $solr_params);
        if ($res && $res->response->numFound <= 0) {
          // Only add if not in Solr index.
          $add = TRUE;
        }
      }
      if ($add) {
        $queue->createItem($object);
      }
    }
  }
}

/**
 * Perform our Sparql query and return an array of PIDs.
 */
function __uofm_batch_index_sparql_query($where) {
  drupal_static_reset('islandora_get_tuque_connection');
  $connection = islandora_get_tuque_connection();

  $query = 'select ?object from <#ri> where { ';
  $query .= $where;
  $query .= ' }';

  $temp = $connection->repository->ri->sparqlQuery($query, 'unlimited');

  $get_pid = function($o) {
    if (is_array($o) && array_key_exists('object', $o)) {
      return $o['object']['value'];
    }
    else {
      ob_start();
      print_r($o);
      $x = ob_get_clean();
      drush_log("Error trying to get value of sparql object ($x)", 'error');
    }
  };

  return array_map($get_pid, $temp);
}

/**
 * Query for the children of an object.
 */
function __uofm_batch_index_get_children($parent) {
  module_load_include('inc', 'islandora', 'includes/utilities');
  $results = array();
  if (islandora_is_valid_pid($parent)) {
    $query = "?object <fedora-rels-ext:isMemberOf> <info:fedora/$parent>";
    $results = __uofm_batch_index_sparql_query($query);
  }
  return $results;
}
