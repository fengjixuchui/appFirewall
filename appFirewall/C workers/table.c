//
//  appFirewall
//
//  Copyright © 2019 Doug Leith. All rights reserved.
//

#include "table.h"
#define STR_SIZE 1024

Hashtable*
hashtable_new(int hint) {
	Hashtable *table;
	int i;
	static unsigned int primes[] = { 509, 509, 1021, 2053, 4093, 8191, 16381, 32771, 65521, 101111, 152501, 250051, INT_MAX };
	for (i = 1; primes[i] < hint; i++);
	table = calloc(1,sizeof(Hashtable) + primes[i-1]*sizeof(Bucket));
	table->size = primes[i-1];
	table->buckets = (Bucket **)(table + 1);
	for (i = 0; i < table->size; i++) table->buckets[i] = NULL;
	return table;
}

void
hashtable_free(Hashtable *table) {
  Bucket *p, *q;
  if (!table) return;
  //printf("hashtable_free: size=%d\n", table->size);
  for (uint32_t i = 0; i < table->size; i++)
    for (p = table->buckets[i]; p; p = q) {
      //printf("hashtable del\n");
      q = p->link;
      if (p->value) {
      	//free(p->value);
 			}
      if (p->key_string) {
       	free(p->key_string);
			}
      free(p);
    }
	free(table);
}

void dump_hashtable(Hashtable *table){
		Bucket *p, *q;
		if (!table) {
			printf("empty table\n");
			return;
		}
		for (uint32_t i = 0; i < table->size; i++) {
			for (p = table->buckets[i]; p; p = q) {
				q = p->link;
				if (p->value) {
					//free(p->value);
				}
				if (p->key_string) {
					printf("key: %u, key_string: '%s'\n", p->key, p->key_string);
				}
			}
		}
}

void*
hashtable_remove(Hashtable *table, const char* key_string) {
	Bucket **pp;
	Key key = hash(key_string);
	uint32_t i = key%table->size;
	for (pp = &table->buckets[i]; *pp; pp = &(*pp)->link)
		if ((key == (*pp)->key) && (strcmp(key_string,(*pp)->key_string)==0) ) {
			// found a match
			Bucket *p = *pp;
			free(p->key_string);
			void *value = p->value;
			*pp = p->link;
			free(p);
			return value;
		}
	return NULL;
}

void*
hashtable_get(Hashtable *table, const char* key_string) {
	Bucket *p;
	Key key = hash(key_string);
	uint32_t i = key%table->size;
	for (p = table->buckets[i]; p; p = p->link)
		if ((key == p->key) && (strcmp(key_string,p->key_string)==0) )
			break;
	return p ? p->value : NULL;
}

void*
hashtable_put(Hashtable *table, const char* key_string, void *value) {
	Bucket *p;
	void *prev;
	Key key = hash(key_string);
	uint32_t i = key%table->size;
	for (p = table->buckets[i]; p; p = p->link)
		if ((key == p->key) && (strcmp(key_string,p->key_string)==0))
			break; // already exists, overwrite value
	if (p == NULL) {
		p = calloc(1, sizeof(Bucket));
		p->key = key;
		int len = (int)strlen(key_string)+1;
		if (len>STR_SIZE) len = STR_SIZE; // just being careful
		p->key_string = malloc(len);
		strlcpy(p->key_string,key_string,len);
		p->link = table->buckets[i];
		table->buckets[i] = p;
		prev = NULL;
	} else
		prev = p->value;
	p->value = value;
	return prev;
}

Key hash(const char *str) {
		// djb2 hash of Dan Bernstein http://www.cse.yorku.ca/~oz/hash.html
    uint32_t hash = 5381;
    int c, count=0;
    while ( (c = *str) && (count<STR_SIZE) ) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
				str++;
		}
    return hash;
}
