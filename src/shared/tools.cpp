// implementation of generic tools

#include "cube.h"

void *operator new(size_t size)
{
    void *p = malloc(size);
    if(!p) abort();
    return p;
}

void *operator new[](size_t size)
{
    void *p = malloc(size);
    if(!p) abort();
    return p;
}

void operator delete(void *p) { if(p) free(p); }

void operator delete[](void *p) { if(p) free(p); }

////////////////////////// rnd numbers ////////////////////////////////////////

#define N (624)             
#define M (397)                
#define K (0x9908B0DFU)       

static uint state[N];
static int next = N;

void seedMT(uint seed)
{
    state[0] = seed;
    for(uint i = 1; i < N; i++)
        state[i] = seed = 1812433253U * (seed ^ (seed >> 30)) + i;
    next = 0;
}

uint randomMT()
{
    int cur = next;
    if(++next >= N)
    {
        if(next > N) { seedMT(5489U + time(NULL)); cur = next++; }
        else next = 0;
    }
    uint y = (state[cur] & 0x80000000U) | (state[next] & 0x7FFFFFFFU);
    state[cur] = y = state[cur < N-M ? cur + M : cur + M-N] ^ (y >> 1) ^ (-int(y & 1U) & K);
    y ^= (y >> 11);
    y ^= (y <<  7) & 0x9D2C5680U;
    y ^= (y << 15) & 0xEFC60000U;
    y ^= (y >> 18);
    return y;
}

///////////////////////// network ///////////////////////

// all network traffic is in 32bit ints, which are then compressed using the following simple scheme (assumes that most values are small).

template<class T>
static inline void putint_(T &p, int n)
{
    if(n<128 && n>-127) p.put(n);
    else if(n<0x8000 && n>=-0x8000) { p.put(0x80); p.put(n); p.put(n>>8); }
    else { p.put(0x81); p.put(n); p.put(n>>8); p.put(n>>16); p.put(n>>24); }
}
void putint(ucharbuf &p, int n) { putint_(p, n); }
void putint(packetbuf &p, int n) { putint_(p, n); }
void putint(vector<uchar> &p, int n) { putint_(p, n); }

int getint(ucharbuf &p)
{
    int c = (schar)p.get();
    if(c==-128) { int n = p.get(); n |= ((schar)p.get())<<8; return n; }
    else if(c==-127) { int n = p.get(); n |= p.get()<<8; n |= p.get()<<16; return n|(p.get()<<24); }
    else return c;
}

// much smaller encoding for unsigned integers up to 28 bits, but can handle signed
template<class T>
static inline void putuint_(T &p, int n)
{
    if(n < 0 || n >= (1<<21))
    {
        p.put(0x80 | (n & 0x7F));
        p.put(0x80 | ((n >> 7) & 0x7F));
        p.put(0x80 | ((n >> 14) & 0x7F));
        p.put(n >> 21);
    }
    else if(n < (1<<7)) p.put(n);
    else if(n < (1<<14))
    {
        p.put(0x80 | (n & 0x7F));
        p.put(n >> 7);
    }
    else
    {
        p.put(0x80 | (n & 0x7F));
        p.put(0x80 | ((n >> 7) & 0x7F));
        p.put(n >> 14);
    }
}
void putuint(ucharbuf &p, int n) { putuint_(p, n); }
void putuint(packetbuf &p, int n) { putuint_(p, n); }
void putuint(vector<uchar> &p, int n) { putuint_(p, n); }

int getuint(ucharbuf &p)
{
    int n = p.get();
    if(n & 0x80)
    {
        n += (p.get() << 7) - 0x80;
        if(n & (1<<14)) n += (p.get() << 14) - (1<<14);
        if(n & (1<<21)) n += (p.get() << 21) - (1<<21);
        if(n & (1<<28)) n |= ~0U<<28;
    }
    return n;
}

template<class T>
static inline void putfloat_(T &p, float f)
{
    lilswap(&f, 1);
    p.put((uchar *)&f, sizeof(float));
}
void putfloat(ucharbuf &p, float f) { putfloat_(p, f); }
void putfloat(packetbuf &p, float f) { putfloat_(p, f); }
void putfloat(vector<uchar> &p, float f) { putfloat_(p, f); }

float getfloat(ucharbuf &p)
{
    float f;
    p.get((uchar *)&f, sizeof(float));
    return lilswap(f);
}

template<class T>
static inline void sendstring_(const char *t, T &p)
{
    while(*t) putint(p, *t++);
    putint(p, 0);
}
void sendstring(const char *t, ucharbuf &p) { sendstring_(t, p); }
void sendstring(const char *t, packetbuf &p) { sendstring_(t, p); }
void sendstring(const char *t, vector<uchar> &p) { sendstring_(t, p); }

void getstring(char *text, ucharbuf &p, size_t len)
{
    char *t = text;
    do
    {
        if(t>=&text[len]) { text[len-1] = 0; return; }
        if(!p.remaining()) { *t = 0; return; }
        *t = getint(p);
    }
    while(*t++);
}

void filtertext(char *dst, const char *src, bool whitespace, bool forcespace, size_t len)
{
    for(int c = uchar(*src); c; c = uchar(*++src))
    {
        if(c == '\f')
        {
            if(!*++src) break;
            continue;
        }
        if(!iscubeprint(c)) // if it's not printable
        {
            if(!iscubespace(c) || !whitespace) continue; // it's not a space or a character, ignore it
            if(forcespace) c = ' ';                      // or, if forcespace, then replace it with a space instead
        }
        *dst++ = c; // add the character
        if(!--len) break; // no more characters left, bail out
    }
    *dst = '\0'; // zero-terminated strings be like -Y
}

void ipmask::parse(const char *name)
{
    union { uchar b[sizeof(enet_uint32)]; enet_uint32 i; } ipconv, maskconv;
    ipconv.i = 0;
    maskconv.i = 0;
    for(int i = 0; i < 4; ++i)
    {
        char *end = NULL;
        int n = strtol(name, &end, 10);
        if(!end) break;
        if(end > name) { ipconv.b[i] = n; maskconv.b[i] = 0xFF; }
        name = end;
        while(int c = *name)
        {
            ++name;
            if(c == '.') break;
            if(c == '/')
            {
                int range = clamp(int(strtol(name, NULL, 10)), 0, 32);
                mask = range ? ENET_HOST_TO_NET_32(0xFFffFFff << (32 - range)) : maskconv.i;
                ip = ipconv.i & mask;
                return;
            }
        }
    }
    ip = ipconv.i;
    mask = maskconv.i;
}

int ipmask::print(char *buf) const
{
    char *start = buf;
    union { uchar b[sizeof(enet_uint32)]; enet_uint32 i; } ipconv, maskconv;
    ipconv.i = ip;
    maskconv.i = mask;
    int lastdigit = -1;
    for(int i = 0; i < 4; ++i) if(maskconv.b[i])
    {
        if(lastdigit >= 0) *buf++ = '.';
        for(int j = 0; j < int(i-lastdigit-1); ++j) { *buf++ = '*'; *buf++ = '.'; }
        buf += sprintf(buf, "%d", ipconv.b[i]);
        lastdigit = i;
    }
    enet_uint32 bits = ~ENET_NET_TO_HOST_32(mask);
    int range = 32;
    for(; (bits&0xFF) == 0xFF; bits >>= 8) range -= 8;
    for(; bits&1; bits >>= 1) --range;
    if(!bits && range%8) buf += sprintf(buf, "/%d", range);
    return int(buf-start);
}

// cURL stuff

struct memoryStruct {
    char* memory;
    size_t size;
};

#ifdef STANDALONE
VAR(offline, 0, 0, 1);
#endif

static size_t writeMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t realsize = size * nmemb;
    struct memoryStruct* mem = (struct memoryStruct*)userp;

    char* ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) {
        /* out of memory! this should never happen */
        conoutf(CON_ERROR, "not enough memory for web operation (realloc returned NULL)");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

VARP(curltimeout, 1, 5, 60);

char* web_auth(char* targetUrl, oldstring gametoken, bool debug)
{
    if (offline && !isdedicatedserver()) {
        if (debug) conoutf(CON_ERROR, "cannot make web request in offline mode");
        return "";
    }
    CURL* curl;
    CURLcode res;

    struct memoryStruct chunk;

    char* url; // thing
    long response_code;

    chunk.memory = (char*)malloc(1);  // will be grown as needed by the realloc above */
    chunk.size = 0;    // no data at this point */

    curl_global_init(CURL_GLOBAL_ALL);

    // init the curl session */
    curl = curl_easy_init();

    struct curl_slist* headers = NULL;

    oldstring gthead;
    formatstring(gthead, "X-Game-Token: %s", gametoken);

    headers = curl_slist_append(headers, gthead);

    // set headers
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // specify URL to get */
    curl_easy_setopt(curl, CURLOPT_URL, targetUrl);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);

    // send all data to this function  */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeMemoryCallback);

    // we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    // some servers don't like requests that are made without a user-agent field, so we provide one
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Cardboard-Engine/1.0.0");

    // set timeout to 5 seconds so the game doesn't break when servers aren't responding
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, curltimeout);

    // get it! */
    res = curl_easy_perform(curl);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);

    // check for errors */
    if (res != CURLE_OK) {
        if (debug) conoutf(CON_ERROR, "curl_easy_perform() failed: %s", curl_easy_strerror(res));
        return "";
    }
    else {
        // we now have the data, do stuff with it
        if (debug) {
            conoutf(CON_INFO, "%lu bytes retrieved", (unsigned long)chunk.size);
            conoutf(CON_INFO, "%d response, %s url", (int)response_code, url);
        }

        return chunk.memory;
    }

    /* cleanup curl stuff */
    curl_easy_cleanup(curl);

    free(chunk.memory);
}

char* web_get(char* targetUrl, bool debug)
{
    if(offline && !isdedicatedserver()) {
        if(debug) conoutf(CON_ERROR, "cannot make web request in offline mode");
        return "";
    }
    CURL* curl;
    CURLcode res;

    struct memoryStruct chunk;

    char* url; // thing
    long response_code;

    chunk.memory = (char*)malloc(1);  // will be grown as needed by the realloc above */
    chunk.size = 0;    // no data at this point */

    curl_global_init(CURL_GLOBAL_ALL);

    // init the curl session */
    curl = curl_easy_init();

    // specify URL to get */
    curl_easy_setopt(curl, CURLOPT_URL, targetUrl);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);

    // send all data to this function  */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeMemoryCallback);

    // we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    // some servers don't like requests that are made without a user-agent field, so we provide one
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Cardboard-Engine/1.0.0");

    // set timeout to 5 seconds so the game doesn't break when servers aren't responding
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, curltimeout);

    // get it! */
    res = curl_easy_perform(curl);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);

    // check for errors */
    if(res != CURLE_OK) {
        if(debug) conoutf(CON_ERROR, "curl_easy_perform() failed: %s", curl_easy_strerror(res));
        return "";
    }
    else {
        // we now have the data, do stuff with it
        if(debug) {
            conoutf(CON_INFO, "%lu bytes retrieved", (unsigned long)chunk.size);
            conoutf(CON_INFO, "%d response, %s url", (int)response_code, url);
        }

        return chunk.memory;
    }

    /* cleanup curl stuff */
    curl_easy_cleanup(curl);

    free(chunk.memory);
}

char* web_post(char* targetUrl, char* postFields, bool debug) // Might work, idk, I don't have an endpoint to test with -Y
{
    if (offline && !isdedicatedserver()) {
        if (debug) conoutf(CON_ERROR, "cannot make web request in offline mode");
        return "";
    }
    CURL* curl;
    CURLcode res;

    struct memoryStruct chunk;

    char* url; // thing
    long response_code;

    chunk.memory = (char*)malloc(1);  // will be grown as needed by the realloc above */
    chunk.size = 0;    // no data at this point */

    curl_global_init(CURL_GLOBAL_ALL);

    // init the curl session */
    curl = curl_easy_init();

    // specify URL to get */
    curl_easy_setopt(curl, CURLOPT_URL, targetUrl);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);

    // send all data to this function  */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeMemoryCallback);

    // we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    // specify the POST data
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields);

    // some servers don't like requests that are made without a user-agent field, so we provide one
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Cardboard-Engine/1.0.0");

    // set timeout to 5 seconds so the game doesn't break when servers aren't responding
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, curltimeout);

    // get it! */
    res = curl_easy_perform(curl);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);

    // check for errors */
    if (res != CURLE_OK) {
        if (debug) conoutf(CON_ERROR, "curl_easy_perform() failed: %s", curl_easy_strerror(res));
        return "";
    }
    else {
        // we now have the data, do stuff with it
        if (debug) {
            conoutf(CON_INFO, "%lu bytes retrieved", (unsigned long)chunk.size);
            conoutf(CON_INFO, "%d response, %s url", (int)response_code, url);
        }

        return chunk.memory;
    }

    /* cleanup curl stuff */
    curl_easy_cleanup(curl);

    free(chunk.memory);
}
