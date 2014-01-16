CONTENTS OF THIS FILE
---------------------

 * summary
 * requirements
 * installation
 * configuration
 * customization
 * troubleshooting
 * faq
 * contact
 * sponsors


SUMMARY
-------

UofM Batch Indexer

Drush command only.

Allows the passing of a Sparql where clause to query the Fedora repository for objects, 
then checks the Solr index for those same PIDs. 

Any missing objects are passed to the FedoraGSearch via a updateFromPid call.

This is maintenance time-saver but... DO NOT USE IF YOU DON'T UNDERSTAND SPARQL QUERIES!!!


REQUIREMENTS
------------

Dependent on:
* islandora
* islandora_solr_search
* islandora_collection_search

INSTALLATION
------------

Install as any other Drupal module.

CONFIGURATION
-------------

Has one parameter available through drush called "query_param", this will be put into the 
Sparql query between the where clauses {}.


CUSTOMIZATION
-------------


TROUBLESHOOTING
---------------


F.A.Q.
------


CONTACT
-------


SPONSORS
--------

