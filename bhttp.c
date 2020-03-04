#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bhttp.h"
#include "bstring.h"
#include "bobject.h"

typedef struct HttpData {
  char *buffer;
  size_t n;
  size_t buffsize;
} HttpData;

static void init_result(HttpData * data) {
  data -> buffer = malloc(CURL_MAX_WRITE_SIZE);
  data -> n = 0;
  data -> buffsize = CURL_MAX_WRITE_SIZE;
}

static void result_add(HttpData * data, char c) {
  if (data -> n >= data -> buffsize) {
    size_t newSize = data -> buffsize * 2;
    data -> buffer = realloc(data -> buffer, newSize);

    if (data -> buffer == NULL) {
      fprintf(stderr, "data too large");
    }
    data -> buffsize = newSize;
  }

  data -> buffer[data -> n++] = c;
}

static void strlwr(char * str) {
  for(int i=0; str[i]!='\0'; i++) {
    if(str[i]>='A' && str[i]<='Z')
      {
        str[i] = str[i] + 32;
      }
  }
}

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  HttpData * data = (HttpData*) userdata;
  size_t i;
  for (i = 0; i < nmemb; i++) {
    result_add(data, ptr[i]);
  }
  return nmemb;
}

static char * application_x_www_form_urlencoded_form(bean_State * B, TValue * data) {
  assert(ttishash(data));
  Hash * hash = hhvalue(data);

  char ** list = (char **)malloc(hash->count * sizeof(char*));
  uint32_t index = 0;
  uint32_t totalSize = 0;
  for (uint32_t i = 0; i < hash->capacity; i++) {
    if (!hash->entries[i]) continue;

    Entry * e = hash->entries[i];
    while (e) {
      TValue * k = tvalue_inspect(B, e->key);
      TValue * v = tvalue_inspect(B, e->value);
      char * key = getstr(svalue(k));
      char * value = getstr(svalue(v));
      uint32_t size = sizeof(key) + sizeof(value);
      totalSize += size + 1; // for & and end of '\0'
      char * content = malloc(size + 2); // equal and '\0'
      sprintf(content, "%s=%s", key, value);
      list[index++] = content;
      e = e -> next;
    }
  }

  char * result = calloc(sizeof(totalSize), '\0');

  for (uint32_t j = 0; j < hash->count; j++) {
    strcat(result, list[j]);
    strcat(result, "&");
  }

  result[strlen(result) - 1] = '\0'; // Remove the last &

  return result;
}

static struct curl_slist * build_header(bean_State * B, TValue * headers) {
  assert(ttishash(headers));
  struct curl_slist *list = NULL;
  Hash * hash = hhvalue(headers);

  for (uint32_t i = 0; i < hash->capacity; i++) {
    if (!hash->entries[i]) continue;

    Entry * e = hash->entries[i];
    while (e) {
      TValue * k = tvalue_inspect(B, e->key);
      TValue * v = tvalue_inspect(B, e->value);
      char * key = getstr(svalue(k));
      char * value = getstr(svalue(v));
      char * content = malloc(sizeof(key) + sizeof(value) + 3); // :, space and \0
      sprintf(content, "%s: %s", key, value);
      list = curl_slist_append(list, content);
      e = e -> next;
    }
  }
  return list;
}

static bool isFormData(bean_State *B, TValue * header) {
  bool formData = false;
  if (header && ttishash(header)) {
    Hash * hash = hhvalue(header);
    for (uint32_t i = 0; i < hash->capacity; i++) {
      if (!hash->entries[i]) continue;

      Entry * e = hash->entries[i];
      while (e) {
        TValue * k = tvalue_inspect(B, e->key);
        TValue * v = tvalue_inspect(B, e->value);
        char * key = getstr(svalue(k));
        char * value = getstr(svalue(v));
        strlwr(key); strlwr(value);
        if (strcmp(key, "content-type") == 0 && strcmp(key, "form-data") == 0) formData = true;
        e = e -> next;
      }
    }
  }
  return formData;
}

static void fetch_data(bean_State *B, char * url, Hash * params, char ** result) {
  CURL *curl;
  CURLcode res;

  curl = curl_easy_init();
  if(curl) {
    HttpData httpdata;
    init_result(&httpdata);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Bean Programming Language/1");
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    /* curl_easy_setopt(curl, CURLOPT_HEADER, 1L); */
    struct curl_slist * headerList = NULL;

    if (params) {
      TValue * m = malloc(sizeof(TValue));
      TString * tsMethod = beanS_newlstr(B, "method", 6);
      setsvalue(m, tsMethod);
      TValue * tvMethod = hash_get(B, params, m);

      TValue * h = malloc(sizeof(TValue));
      TString * tsHeader = beanS_newlstr(B, "header", 6);
      setsvalue(h, tsHeader);
      TValue * tvHeader = hash_get(B, params, h);

      TValue * d = malloc(sizeof(TValue));
      TString * tsData = beanS_newlstr(B, "data", 4);
      setsvalue(d, tsData);
      TValue * tvData = hash_get(B, params, d);

      char * data = NULL;

      if (tvHeader && ttishash(tvHeader)) {
        headerList = build_header(B, tvHeader);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
      }

      if (ttisstring(tvMethod)) {
        char * method = getstr(svalue(tvMethod));
        strlwr(method);

        if (strcmp(method, "post") == 0) {
          bool formData = isFormData(B, tvHeader);

          curl_easy_setopt(curl, CURLOPT_POST, 1L);
          if (formData) {

          } else {
            // application/x-www-form-urlencoded
            // If length is set to 0 (zero), curl_easy_escape uses strlen() on the input string to find out the size.
            data = curl_easy_escape(curl, application_x_www_form_urlencoded_form(B, tvData), 0);
          }
          curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        } else if (strcmp(method, "put") == 0) {
          curl_easy_setopt(curl, CURLOPT_PUT, 1L);
        } else { // GET_METHOD
          curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
          curl_easy_setopt(curl, CURLOPT_WRITEDATA, httpdata);
          /* example.com is redirected, so we tell libcurl to follow redirection */
          curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        }
      }
    }

    /* Perform the request, res will get the return code */
    res = curl_easy_perform(curl);
    /* Check for errors */
    if(res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
    }

    result_add(&httpdata, '\0');

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_slist_free_all(headerList);
    *result = httpdata.buffer;
  }
}

static TValue * primitive_Http_fetch(bean_State * B, TValue * this UNUSED, TValue * args, int argc) {
  assert(argc >= 1);
  TValue * address = &args[0];
  assert(ttisstring(address));
  Hash * hash = NULL;

  if (argc >= 2) { // TODO: Handle the params
    TValue * params = &args[1];
    hash = hhvalue(params);
  }

  char * url = getstr(svalue(address));
  char * result = NULL;
  fetch_data(B, url, hash, &result);
  TString * ts = beanS_newlstr(B, result, strlen(result));
  TValue * value = malloc(sizeof(TValue));
  setsvalue(value, ts);
  return value;
}

TValue * init_Http(bean_State * B) {
  TValue * proto = malloc(sizeof(TValue));
  Hash * h = init_hash(B);
  global_State * G = B->l_G;

  sethashvalue(proto, h);
  set_prototype_function(B, "fetch", 5, primitive_Http_fetch, hhvalue(proto));

  TValue * string = malloc(sizeof(TValue));
  setsvalue(string, beanS_newlstr(B, "HTTP", 4));
  hash_set(B, G->globalScope->variables, string, proto);
  return proto;
}
