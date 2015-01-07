CONTENTS OF THIS FILE
---------------------

 * summary
 * requirements
 * installation
 * examples



SUMMARY
-------

UofM Batch Indexer

Drush command only.

Allows the passing of a PID, list of PIDs, file of PIDs, or Sparql where clause to query 
the Fedora repository for objects, then checks the Solr index for those same PIDs. 

Any missing objects are added to a queue to be passed to the FedoraGSearch via a 
updateFromPid call.

This is maintenance time-saver but... DO NOT USE IF YOU DON'T UNDERSTAND SPARQL QUERIES!!!


REQUIREMENTS
------------

Dependent on:
 * islandora\_solr\_search


INSTALLATION
------------

Install as any other Drupal module.

EXAMPLES
--------

Allows for re-indexing by query or PID list. You must include one of these options:
 * query - a sparql where clause returning ?object
 * pid - a single PID
 * pidlist - a comma deliminated list of PIDs
 * pidfile - a file of PIDs, one per line.
 
It also accepts two optional parameters.
 * force - skip a check of the Solr index for this object
 * recursive - also index any children of the objects being re-indexed.

