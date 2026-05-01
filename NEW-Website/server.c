#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib,"ws2_32.lib")
  typedef int socklen_t;
  #define close closesocket
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
#endif

/* ─── config ─────────────────────────────────────────────── */

#define BUF         65536
#define MAX_CARS    20
#define MAX_DAYS    370
#define CAR_FILE    "CAR.csv"
#define CUST_FILE   "CUSTOMER.csv"

/* ─── send-mail via Python ─────────────────────────────── */
static void shell_escape(const char *src, char *dst, int dstLen) {
    int j = 0;
    for (int i = 0; src[i] && j < dstLen-5; i++) {
        if (src[i] == '\'') {
            /* ' → '\'' */
            dst[j++] = '\'';
            dst[j++] = '\\';
            dst[j++] = '\'';
            dst[j++] = '\'';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = 0;
}

void send_confirmation_email(const char* to_email, const char* refCode, const char* fname, const char* lname, const char* car, const char* start, const char* end, const char* total) {
    
    char esc_fname[128], esc_lname[128], esc_car[128];
    shell_escape(fname, esc_fname, sizeof(esc_fname));
    shell_escape(lname, esc_lname, sizeof(esc_lname));
    shell_escape(car, esc_car, sizeof(esc_car));
    char cmd[2048];

    snprintf(cmd, sizeof(cmd), "python send_email.py \"book\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", to_email, esc_fname, esc_lname, esc_car, start, end, total);
    printf("[MAIL DEBUG] Executing command: %s\n", cmd);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        perror("[MAIL ERROR] Cannot open pipe to python");
        return;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        printf("[PYTHON LOG]: %s", buffer);
    }

    int status = pclose(fp);
    if (status == 0) printf("[MAIL SUCCESS] Email process finished.\n");
    else printf("[MAIL ERROR] Python exited with code %d\n", status);
}

static void send_email_cmd(const char* mode, const char* to, const char* ref, const char* fn, const char* ln, const char* car, const char* st, const char* en, const char* tot) {
    char cmd[2048];
    // เพิ่ม \"%s\" ตัวแรกเพื่อส่ง mode ('book' หรือ 'cancel')
    snprintf(cmd, sizeof(cmd), "python send_email.py \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" &",mode, to, ref, fn, ln, car, st, en, tot);
    system(cmd);
}

/* ─── car struct ──────────────────────────────────────────── */
typedef struct {
    int  number;   /* ลำดับที่ใน CSV (1-based) */
    int  id;       /* ราคา/วัน                 */
    char model[64];
    int  booked[MAX_DAYS]; /* 0=ว่าง 1=จอง */
} Car;

static Car cars[MAX_CARS];
int numCars = 0;

/* ─── helper: leap year ───────────────────────────────────── */
static int isLeap(int y){
    return (y%4==0 && y%100!=0)||(y%400==0);
}
static int daysInMonth(int m,int y){
    int d[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(m==2&&isLeap(y)) return 29;
    return d[m-1];
}
/* แปลง YYYY-MM-DD → day index เทียบกับ 1 ม.ค. ปีเดียวกัน */
static int dateToDayIndex(const char *s){
    if(!s || !s[0]) return -1;

    char tmp[64];
    strcpy(tmp, s);

    //  ลบ newline / carriage return
    tmp[strcspn(tmp, "\r\n")] = 0;

    int y,m,d;
    if(sscanf(tmp,"%d-%d-%d",&y,&m,&d)!=3){
        printf("PARSE FAIL: [%s]\n", tmp);
        return -1;
    }

    int idx=d;
    for(int i=1;i<m;i++) idx+=daysInMonth(i,y);

    int base=2026;
    for(int i=base;i<y;i++){
        idx+=isLeap(i)?366:365;
    }

    return idx;
}

/* ─── load CAR.csv ────────────────────────────────────────── */
static void loadCars(void){
    FILE *fp=fopen(CAR_FILE,"r");
    if(!fp){ fprintf(stderr,"[ERR] cannot open %s\n",CAR_FILE); return; }

    char line[20000];
    int row=0;
    numCars=0;

    while(fgets(line,sizeof(line),fp)){
        row++;
        if(row==1) continue; /* skip header */
        if(numCars>=MAX_CARS) break;

        Car *c=&cars[numCars];
        memset(c,0,sizeof(Car));

        int col=0;
        char *tok=strtok(line,",");
        int dayIdx=0;

        while(tok){
            tok[strcspn(tok,"\n")]=0;
            if(col==0)      c->number=atoi(tok);
            else if(col==1) c->id    =atoi(tok);
            else if(col==2) strncpy(c->model,tok,63);
            else {
                /* col 3 = day:1, col 4 = day:2, … */
                if(dayIdx<MAX_DAYS)
                    c->booked[dayIdx]=atoi(tok);
                dayIdx++;
            }
            tok=strtok(NULL,",");
            col++;
        }
        numCars++;
    }
    fclose(fp);
}

/* ─── save CAR.csv ────────────────────────────────────────── */
static void saveCars(void){
    /* อ่าน header เดิมก่อน */
    FILE *in=fopen(CAR_FILE,"r");
    char header[20000]="";
    if(in){ fgets(header,sizeof(header),in); fclose(in); }

    FILE *fp=fopen(CAR_FILE,"w");
    if(!fp){ fprintf(stderr,"[ERR] cannot write %s\n",CAR_FILE); return; }

    /* เขียน header */
    fprintf(fp,"%s",header);

    for(int i=0;i<numCars;i++){
        Car *c=&cars[i];
        fprintf(fp,"%d,%d,%s",c->number,c->id,c->model);
        for(int d=0;d<MAX_DAYS;d++) fprintf(fp,",%d",c->booked[d]);
        fprintf(fp,"\n");
    }
    fclose(fp);
}

/* ─── check availability (returns 1=available, 0=not) ─────── */
static int isAvailable(int carIdx, int startDay, int endDay){
    Car *c=&cars[carIdx];
    for(int d=startDay;d<=endDay;d++){
        int idx=d-1; /* 0-based array */
        if(idx>=0 && idx<MAX_DAYS && c->booked[idx]!=0) return 0;
    }
    return 1;
}

/* ─── book car ────────────────────────────────────────────── */
static void bookCar(int carIdx, int startDay, int endDay){
    Car *c=&cars[carIdx];
    for(int d=startDay;d<=endDay;d++){
        int idx=d-1;
        if(idx>=0&&idx<MAX_DAYS) c->booked[idx]=1;
    }
    saveCars();
}

/* ─── cancel car ──────────────────────────────────────────── */
static void cancelCar(int carIdx, int startDay, int endDay){
    Car *c=&cars[carIdx];
    for(int d=startDay;d<=endDay;d++){
        int idx=d-1;
        if(idx>=0&&idx<MAX_DAYS) c->booked[idx]=0;
    }
    saveCars();
}

/* ─── append to CUSTOMER.csv ──────────────────────────────── */
static void saveCustomer(
        const char *carModel,
        const char *fname,    const char *lname,
        const char *phone,    const char *email,
        const char *idCard,
        const char *startDate,const char *endDate,
        const char *location,
        const char *payMethod,
        const char *cardName, const char *cardNumber,
        const char *timeOrCvv,const char *expiry,
        const char *total){

    FILE *fp=fopen(CUST_FILE,"a");
    if(!fp){ fprintf(stderr,"[ERR] cannot open %s\n",CUST_FILE); return; }

    time_t now=time(NULL);
    struct tm *t=localtime(&now);
    char today[20];
    strftime(today,sizeof(today),"%Y-%m-%d",t);

    /* ทุก field ห้ามมี comma — sanitize เบื้องต้น */
    #define SAFE(s) ((s)&&(s)[0] ? (s) : "-")

    fprintf(fp,"%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",SAFE(carModel),
        SAFE(fname),   SAFE(lname),
        SAFE(phone),   SAFE(email),
        SAFE(idCard),
        SAFE(startDate),SAFE(endDate),
        SAFE(location),
        today,
        SAFE(payMethod),
        SAFE(cardName), SAFE(cardNumber),
        SAFE(timeOrCvv),SAFE(expiry),
        SAFE(total));
    #undef SAFE

    fclose(fp);
}

/* ─── delete customer from CSV ────────────────────────────── */
static int deleteCustomer(const char *fname, const char *lname, int *outCarIdx, int *outStart, int *outEnd, char *outEmail, char *outModel) {
    FILE *fp=fopen(CUST_FILE,"r");
    if(!fp) return 0;

    char lines[500][512];
    int count=0;
    int foundLine=-1;
    loadCars();
    while(fgets(lines[count],512,fp) && count<499){
        char tmp[512];
        strcpy(tmp,lines[count]);
        char *model=strtok(tmp,",");
        char *fn   =strtok(NULL,",");
        char *ln   =strtok(NULL,",");
        
        if(fn&&ln){
            fn[strcspn(fn,"\n")]=0;
            ln[strcspn(ln,"\n")]=0;
            if(strcmp(fn,fname)==0&&strcmp(ln,lname)==0){
                foundLine=count;
                /* หา carIdx, start, end */
                char tmp2[512]; strcpy(tmp2,lines[count]);
                char *m2   =strtok(tmp2,",");
                char *fn2  =strtok(NULL,",");
                char *ln2  =strtok(NULL,",");
                char *ph2  =strtok(NULL,",");
                char *em2  =strtok(NULL,",");
                char *idcard  =strtok(NULL,",");
                char *sd   =strtok(NULL,",");
                char *ed   =strtok(NULL,",");
                if(sd&&ed){
                    sd[strcspn(sd,"\n")]=0;
                    ed[strcspn(ed,"\n")]=0;
                    *outStart=dateToDayIndex(sd);
                    *outEnd  =dateToDayIndex(ed);
                }
                if (m2) strcpy(outModel, m2);
                if (em2) strcpy(outEmail, em2);
                /* หา car index จาก model name */
                *outCarIdx = -1;
                for (int i = 0; i < numCars; i++) {
                    if (strcmp(cars[i].model, m2) == 0) {
                        *outCarIdx = i;
                        break;
                    }
                }
            }
        }
        count++;
    }
    fclose(fp);
    if(foundLine<0) return 0;

    /* เขียนไฟล์ใหม่โดยข้ามบรรทัดนั้น */
    FILE *out=fopen(CUST_FILE,"w");
    if(!out) return 0;
    for(int i=0;i<count;i++){
        if(i!=foundLine) fprintf(out,"%s",lines[i]);
    }
    fclose(out);
    return 1;
}


/* ══════════════════════════════════════════════════════════════
                        HTTP helpers
   ══════════════════════════════════════════════════════════════ */

static void sendResponse(int sock, int code, const char *body){
    const char *status =  code==200 ? "200 OK"
                        : code==400 ? "400 Bad Request"
                        : code==404 ? "404 Not Found"
                        :             "500 Internal Server Error";
    char header[512];
    int hlen=snprintf(header,sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, strlen(body));
    send(sock,header,hlen,0);
    send(sock,body,strlen(body),0);
}

/* ดึง query param value จาก URL  ?key=value&... */
static int getParam(const char *url, const char *key, char *out, int outLen){
    char search[64];
    snprintf(search,sizeof(search),"%s=",key);
    const char *p=strstr(url,search);
    if(!p) return 0;
    p+=strlen(search);
    const char *end=strpbrk(p,"& \r\n\0");
    int len=end ? (int)(end-p) : (int)strlen(p);
    if(len>=outLen) len=outLen-1;
    strncpy(out,p,len);
    out[len]=0;
    return 1;
}

/* ดึง JSON string value  "key":"value" */
static int getJsonStr(const char *json, const char *key, char *out, int outLen){
    char search[64];
    snprintf(search,sizeof(search),"\"%s\"",key);
    const char *p=strstr(json,search);
    if(!p) return 0;
    p+=strlen(search);
    /* skip whitespace and colon */
    while(*p && (*p==':'||*p==' '||*p=='\t')) p++;
    if(*p!='"') return 0;
    p++;
    int i=0;
    /* copy bytes until closing quote, handling \uXXXX and escaped chars
       UTF-8 bytes are > 0x7F — copy them as-is (opaque)               */
    while(*p && i<outLen-1){
        unsigned char ch = (unsigned char)*p;
        if(ch == '"') break;          /* end of string */
        if(ch == '\\'){               /* escape sequence */
            p++;
            ch = (unsigned char)*p;
            switch(ch){
                case '"':  out[i++]='"';  break;
                case '\\': out[i++]='\\'; break;
                case '/':  out[i++]='/';  break;
                case 'n':  out[i++]='\n'; break;
                case 'r':  out[i++]='\r'; break;
                case 't':  out[i++]='\t'; break;
                case 'u':{
                    /* \uXXXX — copy raw bytes for now, JS already sent UTF-8 */
                    out[i++]='?'; p+=4; /* skip 4 hex digits */
                    break;
                }
                default: out[i++]=(char)ch; break;
            }
        } else {
            /* UTF-8: copy all bytes of this character (may be 1-4 bytes) */
            out[i++]=(char)ch;
        }
        p++;
    }
    out[i]=0;
    return i>0 || *p=='"'; /* return 1 even for empty string */
}

static int getJsonInt(const char *json, const char *key, int *out){
    char buf[32];
    char search[64];
    snprintf(search,sizeof(search),"\"%s\"",key);
    const char *p=strstr(json,search);
    if(!p) return 0;
    p+=strlen(search);
    while(*p && (*p==':'||*p==' ')) p++;
    int i=0;
    while(*p && (*p=='-'||((*p>='0')&&(*p<='9'))) && i<31) buf[i++]=*p++;
    buf[i]=0;
    *out=atoi(buf);
    return 1;
}

/* แปลง day index กลับเป็น YYYY-MM-DD (base year 2026) */
static void dayIndexToDate(int dayIdx, char *out){
    int year=2026;
    int remaining=dayIdx;
    int daysInYear;
    while(1){
        daysInYear=isLeap(year)?366:365;
        if(remaining<=daysInYear) break;
        remaining-=daysInYear;
        year++;
    }
    int month=0;
    int d[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(isLeap(year)) d[1]=29;
    while(remaining>d[month]){ remaining-=d[month]; month++; }
    sprintf(out,"%04d-%02d-%02d",year,month+1,remaining);
}

/* ══════════════════════════════════════════════════════════════
                        Handlers
   ══════════════════════════════════════════════════════════════ */

static void handleAvailability(int sock, const char *url){
    char startStr[20]="", endStr[20]="";
    getParam(url,"start",startStr,20);
    getParam(url,"end",  endStr,  20);

    int s=dateToDayIndex(startStr);
    int e=dateToDayIndex(endStr);
    if(s<0||e<0||e<s){
        sendResponse(sock,400,"{\"ok\":false,\"error\":\"invalid date range\"}");
        return;
    }

    loadCars();

    char body[8192];
    int pos=0;
    pos+=snprintf(body+pos,sizeof(body)-pos,
        "{\"ok\":true,\"startDay\":%d,\"endDay\":%d,\"cars\":[",s,e);

    for(int i=0;i<numCars;i++){
        int avail=isAvailable(i,s,e);
        pos+=snprintf(body+pos,sizeof(body)-pos,
            "%s{\"number\":%d,\"pricePerDay\":%d,\"model\":\"%s\",\"available\":%s}",
            i>0?",":"",
            cars[i].number,
            cars[i].id,
            cars[i].model,
            avail?"true":"false");
    }
    pos+=snprintf(body+pos,sizeof(body)-pos,"]}");
    sendResponse(sock,200,body);
}

/*  POST /book
    Body JSON:
    { "carNumber":1, "startDate":"2026-04-15", "endDate":"2026-04-17",
        "firstName":"สมชาย", "lastName":"ใจดี",
        "phone":"0812345678", "email":"a@gmail.com", "delivery":"สมุทรสาคร-เซ็นทรัลมหาชัย" }
*/
static void handleBook(int sock, const char *body){
    int carNumber=0;
    char startDate[20]="",  endDate[20]="";
    char fname[256]="",     lname[256]="",   phone[32]="",    email[128]="";
    char idCard[32]="",     location[80]="", payMethod[20]="";
    char cardName[128]="",  cardNumber[32]="";
    char timeOrCvv[16]="",  expiry[12]="",   total[20]="";

    /* carNumber / carId */
    if(!getJsonInt(body,"carNumber",&carNumber))
        getJsonInt(body,"carId",&carNumber);

    /* dates & location */
    getJsonStr(body,"startDate",   startDate,  20);
    getJsonStr(body,"endDate",     endDate,    20);
    getJsonStr(body,"deliveryValue",location,  80);

    /* customer */
    if(!getJsonStr(body,"firstName",fname,256)) getJsonStr(body,"first_name",fname,256);
    if(!getJsonStr(body,"lastName", lname,256)) getJsonStr(body,"last_name", lname,256);
    getJsonStr(body,"phone",   phone,   32);
    getJsonStr(body,"email",   email,   128);
    getJsonStr(body,"idCard",  idCard,  32);

    /* payment */
    getJsonStr(body,"payMethod",   payMethod,  20);
    getJsonStr(body,"cardName",    cardName,   128);
    getJsonStr(body,"cardNumber",  cardNumber, 32);
    getJsonStr(body,"timeOrCvv",   timeOrCvv,  16);
    getJsonStr(body,"expiry",      expiry,     12);
    getJsonStr(body,"total",       total,      20);

    printf("[BOOK] carNumber=%d start=%s end=%s fname=%s lname=%s pay=%s total=%s\n",carNumber, startDate, endDate, fname, lname, payMethod, total);

    /* validate */
    if(carNumber<1||!startDate[0]||!endDate[0]||!fname[0]||!lname[0]){
        sendResponse(sock,400,"{\"ok\":false,\"error\":\"missing fields\"}");
        return;
    }

    loadCars();

    int carIdx=-1;
    for(int i=0;i<numCars;i++){
        if(cars[i].number==carNumber){ carIdx=i; break; }
    }
    if(carIdx<0){
        sendResponse(sock,400,"{\"ok\":false,\"error\":\"car not found\"}");
        return;
    }

    int s=dateToDayIndex(startDate);
    int e=dateToDayIndex(endDate);
    if(s<0||e<0||e<s){
        sendResponse(sock,400,"{\"ok\":false,\"error\":\"invalid dates\"}");
        return;
    }

    if(!isAvailable(carIdx,s,e)){
        sendResponse(sock,200,"{\"ok\":false,\"error\":\"car not available on selected dates\"}");
        return;
    }

    bookCar(carIdx,s,e);

    srand((unsigned)time(NULL));
    char refCode[16];
    snprintf(refCode,sizeof(refCode),"RM-%06d",100000+(rand()%900000));

    /* บันทึก customer พร้อม fields ใหม่ทั้งหมด */
    saveCustomer(cars[carIdx].model, fname, lname, phone, email, idCard, startDate, endDate, location, payMethod, cardName, cardNumber, timeOrCvv, expiry, total);

    printf("[MAIL] sendto: %s\n", email);
    send_confirmation_email(
    email,
    refCode,
    fname,                    
    lname,                     
    cars[carIdx].model,       
    startDate,                 
    endDate,                  
    total                     
    );

    int numDays=(e-s)+1;
    if(numDays<1) numDays=1;

    char resp[256];
    snprintf(resp,sizeof(resp),"{\"ok\":true,\"refCode\":\"%s\",\"numDays\":%d}",refCode, numDays);
    sendResponse(sock,200,resp);
}

static void handleCancel(int sock, const char *body) {
    char fname[64] = "", lname[64] = "";
    getJsonStr(body,"firstName",fname,64);
    getJsonStr(body,"lastName", lname,64);

    int carIdx = -1, s = -1, e = -1;
    char email[128] = "", model[128] = "";

    if (deleteCustomer(fname, lname, &carIdx, &s, &e, email, model)) {
        if (carIdx >= 0) {
            cancelCar(carIdx, s, e); 
        }
        char sDate[20], eDate[20];
        dayIndexToDate(s, sDate);
        dayIndexToDate(e, eDate);
        send_email_cmd("cancel", email, "CANCELLED", fname, lname, model, sDate, eDate, "0");
        sendResponse(sock, 200, "{\"ok\":true}");
    } else {
        printf("[DEBUG] not found: %s %s\n", fname, lname);
        sendResponse(sock, 200, "{\"ok\":false,\"error\":\"ไม่พบข้อมูลการจอง โปรดตรวจสอบชื่อและนามสกุล\"}");
    }
}

void handleMyBookings(int client, const char *jsonBody) {
    /* ── parse ชื่อ-นามสกุลจาก JSON ด้วย getJsonStr ที่รองรับ UTF-8 ── */
    char reqF[256]="", reqL[256]="";
    if(!getJsonStr(jsonBody,"firstName",reqF,256))
        getJsonStr(jsonBody,"first_name",reqF,256);
    if(!getJsonStr(jsonBody,"lastName",reqL,256))
        getJsonStr(jsonBody,"last_name",reqL,256);

    if(!reqF[0]||!reqL[0]){
        sendResponse(client,400,"{\"ok\":false,\"error\":\"missing name\"}");
        return;
    }

    FILE *f = fopen(CUST_FILE, "r");
    if(!f) {
        fprintf(stderr,"[ERR] cannot open %s\n", CUST_FILE);
        sendResponse(client, 500, "{\"ok\":false,\"error\":\"Cannot open customer database\"}");
        return;
    }

    /* ── buffer JSON response ── */
    /* NEW CUSTOMER.csv columns (0-based, 16 total):
        0=car       1=fname      2=lname      3=phone      4=email
        5=idCard    6=startDate  7=endDate    8=location   9=recordDate
        10=payMethod 11=cardName 12=cardNumber 13=timeOrCvv 14=expiry 15=total */
    char resbuf[32768];
    int  pos   = 0;
    int  found = 0;

    pos += snprintf(resbuf+pos, sizeof(resbuf)-pos, "{\"ok\":true,\"bookings\":[");

    char line[2048];
    int isHeader = 1;

    while(fgets(line, sizeof(line), f)){
        if(isHeader){ isHeader=0; continue; }
        if(line[0]=='\n'||line[0]=='\r'||line[0]=='\0') continue;

        char tmp[2048];
        strncpy(tmp, line, sizeof(tmp)-1);
        tmp[sizeof(tmp)-1]=0;

        char *col[16];
        int   nc = 0;
        char *tok = strtok(tmp, ",");
        while(tok && nc < 16){
            tok[strcspn(tok,"\r\n")] = 0;
            col[nc++] = tok;
            tok = strtok(NULL, ",");
        }
        if(nc < 8) continue;

        if(strcmp(col[1], reqF)!=0 || strcmp(col[2], reqL)!=0) continue;

        /* escape double-quotes in car name */
        char carEsc[128]="";
        int  ei = 0;
        for(int k=0; col[0][k] && ei<126; k++){
            if(col[0][k]=='"') carEsc[ei++]='\\';
            carEsc[ei++] = col[0][k];
        }
        carEsc[ei]=0;

        /* new column layout */
        const char *startDate  = (nc>6)  ? col[6]  : "";
        const char *endDate    = (nc>7)  ? col[7]  : "";
        const char *location   = (nc>8)  ? col[8]  : "";
        const char *recordDate = (nc>9)  ? col[9]  : "";
        const char *payMethod  = (nc>10) ? col[10] : "";
        const char *total      = (nc>15) ? col[15] : "";

        pos += snprintf(resbuf+pos, sizeof(resbuf)-pos,
            "%s{\"car\":\"%s\","
            "\"startDate\":\"%s\","
            "\"endDate\":\"%s\","
            "\"delivery\":\"%s\","
            "\"recordDate\":\"%s\","
            "\"payMethod\":\"%s\","
            "\"total\":\"%s\"}",
            found ? "," : "",
            carEsc, startDate, endDate, location, recordDate, payMethod, total);
        found++;
    }
    fclose(f);

    pos += snprintf(resbuf+pos, sizeof(resbuf)-pos, "]}");
    printf("[MYBOOKINGS] fname=%s lname=%s found=%d\n", reqF, reqL, found);
    sendResponse(client, 200, resbuf);
}

static void handleAllBookings(int sock, const char *url) {

    /* ── parse query params ── */
    char qCar[64]="", qFrom[20]="", qTo[20]="", qStatus[16]="", qMethod[32]="", qLoc[80]="";
    getParam(url, "car",      qCar,    64);
    getParam(url, "dateFrom", qFrom,   20);
    getParam(url, "dateTo",   qTo,     20);
    getParam(url, "status",   qStatus, 16);
    getParam(url, "method",   qMethod, 32);
    getParam(url, "location", qLoc,    80);

    /* URL-decode '+' → space for Thai text (basic) */
    for(char *p=qCar;  *p; p++) if(*p=='+') *p=' ';
    for(char *p=qMethod;*p;p++) if(*p=='+') *p=' ';
    for(char *p=qLoc;  *p; p++) if(*p=='+') *p=' ';

    FILE *f = fopen(CUST_FILE, "r");
    if(!f){
        fprintf(stderr,"[ERR] cannot open %s\n", CUST_FILE);
        sendResponse(sock,500,"{\"ok\":false,\"error\":\"Cannot open customer database\"}");
        return;
    }
    
    /* ── read ALL data rows into memory ── */
    /* NEW column layout (16 cols):
        0=car  1=fname  2=lname  3=phone  4=email  5=idCard
        6=startDate  7=endDate  8=location  9=recordDate
        10=payMethod  11=cardName  12=cardNumber  13=timeOrCvv  14=expiry  15=total */
    #define MAX_ROWS 2000
    #define MAX_COL  16

    /* store lines as flat char arrays */
    typedef struct { char col[MAX_COL][256]; int nc; } Row;
    Row *rows = (Row*)malloc(sizeof(Row) * MAX_ROWS);
    if(!rows){ fclose(f); sendResponse(sock,500,"{\"ok\":false,\"error\":\"OOM\"}"); return; }

    char line[2048];
    int totalRows = 0;
    int isHeader  = 1;

    while(fgets(line, sizeof(line), f) && totalRows < MAX_ROWS){
        if(isHeader){ isHeader=0; continue; }
        if(line[0]=='\n'||line[0]=='\r'||line[0]=='\0') continue;

        char tmp[2048];
        strncpy(tmp, line, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;

        Row *r = &rows[totalRows];
        r->nc  = 0;
        char *tok = strtok(tmp, ",");
        while(tok && r->nc < MAX_COL){
            tok[strcspn(tok,"\r\n")] = 0;
            strncpy(r->col[r->nc], tok, 255);
            r->col[r->nc][255] = 0;
            r->nc++;
            tok = strtok(NULL, ",");
        }
        if(r->nc >= 8) totalRows++;
    }
    fclose(f);

    /* ── helper: today string ── */
    time_t nowt = time(NULL);
    struct tm *nowtm = localtime(&nowt);
    char todayStr[20];
    strftime(todayStr, sizeof(todayStr), "%Y-%m-%d", nowtm);

    char *resbuf = (char*)malloc(1024*1024); /* 1MB */
    if(!resbuf){ free(rows); sendResponse(sock,500,"{\"ok\":false,\"error\":\"OOM\"}"); return; }

    int pos   = 0;
    int found = 0;
    int total_revenue = 0;

    pos += snprintf(resbuf+pos, 1024*1024-pos, "{\"ok\":true,\"bookings\":[");

    for(int i = totalRows-1; i >= 0; i--){
        Row *r = &rows[i];
        char *car        = r->col[0];
        char *fname      = r->col[1];
        char *lname      = r->col[2];
        char *phone      = r->col[3];
        char *email      = r->col[4]; 
        char *idCard     = (r->nc>5) ? r->col[5] : "";
        char *startDate  = (r->nc>6) ? r->col[6] : "";
        char *endDate    = (r->nc>7) ? r->col[7] : "";
        char *location   = (r->nc>8) ? r->col[8] : "";
        char *recordDate = (r->nc>9) ? r->col[9] : "";
        char *payMethod  = (r->nc>10)? r->col[10]: "";
        char *cardName   = (r->nc>11)? r->col[11]: "";
        char *cardNumber = (r->nc>12)? r->col[12]: "";
        char *cvv        = (r->nc>13)? r->col[13]: "";
        char *expiry     = (r->nc>14)? r->col[14]: "";
        char *totalStr   = (r->nc>15)? r->col[15]: "0";

        /* ── compute row status ── */
        char rowStatus[12] = "past";
        if(startDate[0] && endDate[0]){
            if(strcmp(todayStr, startDate) < 0)       strcpy(rowStatus,"upcoming");
            else if(strcmp(todayStr, endDate) <= 0)   strcpy(rowStatus,"active");
        }

        /* ── apply filters ── */
        /* car filter (case-insensitive substring) */
        if(qCar[0]){
            char carLower[64]="", qLower[64]="";
            for(int k=0;car[k]&&k<63;k++)    carLower[k] = (char)tolower((unsigned char)car[k]);
            for(int k=0;qCar[k]&&k<63;k++)   qLower[k]   = (char)tolower((unsigned char)qCar[k]);
            if(!strstr(carLower,qLower)) continue;
        }

        if(qFrom[0] && startDate[0] && strcmp(startDate, qFrom) < 0) continue;
        if(qTo[0]   && startDate[0] && strcmp(startDate, qTo)   > 0) continue;
        if(qStatus[0] && strcmp(rowStatus, qStatus)!=0) continue;
        if(qMethod[0] && strcmp(payMethod, qMethod)!=0) continue;
        if(qLoc[0] && !strstr(location, qLoc)) continue;

        char carEsc[256]=""; int ei=0;
        for(int k=0;car[k]&&ei<254;k++){ if(car[k]=='"') carEsc[ei++]='\\'; carEsc[ei++]=car[k]; }

        total_revenue += atoi(totalStr);

        pos += snprintf(resbuf + pos, 1024 * 1024 - pos,
        "%s{"
        "\"car\":\"%s\","
        "\"fname\":\"%s\","
        "\"lname\":\"%s\","
        "\"phone\":\"%s\","
        "\"email\":\"%s\","
        "\"id\":\"%s\","        
        "\"startDate\":\"%s\","
        "\"endDate\":\"%s\","
        "\"location\":\"%s\","
        "\"recordDate\":\"%s\","
        "\"payMethod\":\"%s\","
        "\"nameofcard\":\"%s\","  
        "\"numofcard\":\"%s\","   
        "\"cvv\":\"%s\","         
        "\"exp\":\"%s\","         
        "\"total\":\"%s\","
        "\"status\":\"%s\""
        "}",
            found ? "," : "",
            carEsc, fname, lname, phone, email, idCard, 
            startDate, endDate, location, recordDate, payMethod,
            cardName, cardNumber, cvv, expiry, totalStr, rowStatus);
        found++;
    }

    pos += snprintf(resbuf+pos, 1024*1024-pos,"],\"totalRows\":%d,\"totalRevenue\":%d}", found, total_revenue);

    free(rows);
    printf("[ADMIN] allbookings found=%d\n", found);
    sendResponse(sock, 200, resbuf);
    free(resbuf);
    #undef MAX_ROWS
    #undef MAX_COL
}


/* ══════════════════════════════════════════════════════════════
                        Main server loop
   ══════════════════════════════════════════════════════════════ */
int main(void){
int PORT = 8080;
char* portEnv = getenv("PORT");
if (portEnv != NULL) {
    PORT = atoi(portEnv);}

#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2),&wsa);
#endif

    int server=socket(AF_INET,SOCK_STREAM,0);
    if(server<0){ perror("socket"); return 1; }

    int opt=1;
    setsockopt(server,SOL_SOCKET,SO_REUSEADDR, (const char*)&opt,sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family     =AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port       =htons(PORT);

    if(bind(server,(struct sockaddr*)&addr,sizeof(addr))<0){
        perror("bind"); return 1;
    }
    listen(server,10);
    printf("[INFO] RODCHAOMAHACHAI backend running on http://localhost:%d\n",PORT);
    printf("[INFO] Place CAR.csv and CUSTOMER.csv in the same directory.\n");
    printf("[INFO] Press Ctrl+C to stop.\n");

    while(1){
        struct sockaddr_in caddr;
        socklen_t clen=sizeof(caddr);
        int client=accept(server,(struct sockaddr*)&caddr,&clen);
        if(client<0) continue;

        char req[BUF];
        int n = recv(client, req, BUF - 1, 0);
        if (n <= 0) { close(client); continue; }
        req[n] = 0;

        /* --- โค้ดส่วนที่เพิ่ม: รอรับข้อมูล JSON Body ให้ครบ --- */
        char *header_end = strstr(req, "\r\n\r\n");
        char *cl_ptr = strstr(req, "Content-Length: ");
        int content_len = cl_ptr ? atoi(cl_ptr + 16) : 0;
        
        if (header_end && content_len > 0) {
            int header_len = (header_end - req) + 4;
            while ((n - header_len) < content_len && n < BUF - 1) {
                int more = recv(client, req + n, BUF - 1 - n, 0);
                if (more <= 0) break;
                n += more;
                req[n] = 0;
            }
        }
        /* ------------------------------------------------ */

        /* แยก method และ path */
        char method[8]="", path[256]="";
        sscanf(req,"%7s %255s",method,path);

        printf("[REQ] %s %s\n",method,path);

        /* OPTIONS preflight (CORS) */
        if(strcmp(method,"OPTIONS")==0){
            sendResponse(client,200,"{}");
            close(client); continue;
        }

        /* หา body (หลัง \r\n\r\n) */
        const char *bodyStart=strstr(req,"\r\n\r\n");
        const char *jsonBody = bodyStart ? bodyStart+4 : "";

        if(strncmp(path,"/availability",13)==0){
            handleAvailability(client, path);
        }
        else if(strcmp(path,"/book")==0 && strcmp(method,"POST")==0){
            handleBook(client, jsonBody);
        }
        else if(strcmp(path,"/cancel")==0 && strcmp(method,"POST")==0){
            handleCancel(client, jsonBody);
        }
        else if(strcmp(path,"/mybookings")==0 && strcmp(method,"POST")==0){
            handleMyBookings(client, jsonBody);
        }
        else if(strncmp(path,"/allbookings",12)==0 && strcmp(method,"GET")==0){
            handleAllBookings(client, path);
        }
        else if(strcmp(path,"/status")==0){
            sendResponse(client,200,"{\"ok\":true,\"service\":\"RODCHAOMAHACHAI\"}");
        }
        else{
            sendResponse(client,404,"{\"ok\":false,\"error\":\"not found\"}");
        }

        close(client);
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
