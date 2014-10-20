#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#define SUCCESS 0
#define CONNFAIL -1
#define AUTHFAIL -2
#define TOPFAIL -1
int auth(int, char*, char*);
int stat(int);
int retrToFile (int, int, int, int);
int getHeader (int, int, char*);
int getHeaderValue(const char*, const char*, char*);
void quit(int);

void main(int argc, char *argv[]) {
    int i, j, k;
    int mailNum;
    int sock;
    int status;
    int fd;
    struct addrinfo hints, *results;
    //Mengecek jumlah argumen
    if (argc < 2) {
        printf("Usage: ./pop3 ADDRESS USERNAME PASSWORD\n");
        exit(-1);
    }
    char *address = argv[1]; //hostname
    char *user = argv[2];
    char *pass = argv[3];
    char filename[1024];
    char temp[1024];
    char headerBuf[8192];
    //Mencoba mencari address info dari hostname yang diberikan
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;
    status = getaddrinfo(address, "pop3", &hints, &results);
    if (status != 0) {
        printf("Error getting address info: %d\n", status);
        exit (-1);
    }
    //address info telah didapat, selanjutnya buat socket dan lakukan koneksi ke alamat yang dituju
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    status = connect(sock, results->ai_addr, results->ai_addrlen);
    if (status != 0) {
        perror("Error while trying to connect to server");
        exit (-1);
    }
    //Setelah koneksi berhasil dibuat, lakukan autentikasi
    status = auth(sock, user, pass);
    if (status == CONNFAIL) {
        printf("Error: connection refused\n");
        exit (-1);
    }
    else if (status == AUTHFAIL) {
        printf("Error: invalid user / pass\n");
        exit (-1);
    }
    //Autentikasi berhasil, cek jumlah mail yang ada di mailbox
    mailNum = stat(sock);
    printf("Hi %s! You have %d new messages:\n", user, mailNum);
    //Berhasil mendapatkan jumlah mail yang ada di mailbox, tampilkan headernya. (status berisi jumlah mail)
    for (i=1; i<=mailNum; i++) {
        status = getHeader(sock, i, headerBuf);
        printf("%d.  ", i);
        if (status == SUCCESS) {
            status = getHeaderValue(headerBuf, "Subject", temp);
            if (status == SUCCESS )
                printf("%s", temp);
            else
                printf("<no subject>");
            status = getHeaderValue(headerBuf, "From", temp);
            if (status == SUCCESS )
                printf(" (%s)", temp);
            else
                printf(" (<unknown sender>)");
            status = getHeaderValue(headerBuf, "Date", temp);
            if (status == SUCCESS )
                printf(" -- on %s\n", temp);
            else
                printf("\n");
        }
    }
    //Baca input, mail mana yang ingin disimpan oleh user
    printf("Which mail do you want to save? (type 0 to exit) ");
    scanf("%d", &i);
    int deleteMail;
    while (i != 0) {
        printf("Save as: ");
        scanf("%s", filename);
        printf("Delete mail in the mailbox? (0: no/1: yes) ");
        scanf("%d", &deleteMail);
        //Buat file dengan permission read & write untuk user yang bersangkutan
        fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            perror("Error");
        }
        else {
            retrToFile(sock, i, fd, deleteMail);
            close(fd);
        }
        //Ulangi
        printf("Which mail do you want to save? (type 0 to exit) ");
        scanf("%d", &i);
    }
    //Selesai, tutup socket
    quit(sock);
    close(sock);
}

/*
    Fungsi untuk meng-autentikasi user.
    parameter:
        - sock adalah file descriptor dari socket yang telah dibuat.
        - user adalah string username, digunakan sebagai parameter pada statement "user".
        - pass adalah string password, digunakan sebagai parameter pada statement "pass".
    return value:
        - SUCCESS apabila user berhasil terautentikasi dengan string user dan pass yang disediakan
        - CONNFAIL apabila tidak berhasil melakukan koneksi ke server (closed / blocked port)
        - AUTHFAIL apabila dapat melakukan koneksi ke server, namun gagal mengautentikasi dengan kombinasi user dan pass
          yang disediakan
*/
int auth(int sock, char *user, char *pass) {
    char buf[256], rbuf[512];
    int bytes;
    bytes = recv(sock, rbuf, sizeof(rbuf), 0);
    if (rbuf[0] == '-')
        return CONNFAIL;

    sprintf(buf, "user %s\r\n", user);
    send(sock, buf, strlen(buf), 0);
    bytes = recv(sock, rbuf, sizeof(rbuf), 0);

    if (rbuf[0] == '-')
        return AUTHFAIL;

    sprintf(buf, "pass %s\r\n", pass);
    send(sock, buf, strlen(buf), 0);
    bytes = recv(sock, rbuf, sizeof(rbuf), 0);

    if (rbuf[0] == '-')
        return AUTHFAIL;
    else
        return SUCCESS;
}

/*
    Fungsi untuk mencari tahu berapa mail yang ada di mailbox dari user yang telah login.
    pre: fungsi auth() sudah dipanggil dengan return value SUCCESS
    parameter:
        - sock adalah file descriptor dari socket yang telah dibuat.
    return value: jumlah dari mail yang ada di mailbox.
*/
int stat (int sock) {
    char buf[100], rbuf[512];
    int num, bytes;
    sprintf(buf, "stat\r\n");
    send(sock, buf, strlen(buf), 0);
    bytes = recv(sock, rbuf, sizeof(rbuf), 0);
    rbuf[bytes-1] = 0;
    sscanf(rbuf, "+OK %d", &num);
    return num;
}

/*
    Fungsi untuk mendapatkan isi dari mail (beserta headernya) dan menulisnya ke file.
    parameter:
        - sock adalah file descriptor dari socket yang telah dibuat.
        - mailId adalah nomor mail yang ingin disimpan.
        - fd adalah file descriptor dari file yang digunakan untuk menyimpan isi mail.
*/
int retrToFile (int sock, int mailId, int fd, int deleteMail) {
    char buf[100], rbuf[10], tbuf[25]="";
    int bytes;
    sprintf(buf, "retr %d\r\n", mailId);
    send(sock, buf, strlen(buf), 0);
    while ( (bytes = recv(sock, rbuf, 9, 0)) > 0) {
        rbuf[bytes] = 0;
        strcat(tbuf, rbuf);
        if (strstr(tbuf, "\r\n.\r\n") != NULL) {
            write(fd, rbuf, bytes);
            break;
        }
        strcpy(tbuf, rbuf);
        write(fd, rbuf, bytes);
    }

    if(deleteMail) {
        sprintf(buf, "dele %d\r\n", mailId);
        write(sock, buf, strlen(buf));
        bytes = read(sock, rbuf, 1024);
    }

    return SUCCESS;
}


/*
    Fungsi untuk mendapatkan header dari mail dengan nomor mailId.
    parameter:
        - sock adalah file descriptor dari socket yang telah dibuat.
        - mailId adalah nomor mail yang ingin dilihat headernya.
        - header adalah string output dari fungsi ini.
    return value:
        SUCCESS apabila tidak terjadi masalah,
        TOPFAIL apabila terjadi masalah (misal mail dengan mailId yang dieberikan tidak ada)
*/
int getHeader (int sock, int mailId, char *header) {
    char buf[100], rbuf[10], tbuf[25]="";
    int bytes;
    sprintf(buf, "top %d 0\r\n", mailId);
    send(sock, buf, strlen(buf), 0);
    header[0] = 0;
    while ( (bytes = recv(sock, rbuf, 9, 0)) > 0) {
        rbuf[bytes] = 0;
        strcat(tbuf, rbuf);
        if (strstr(tbuf, "\r\n.\r\n") != NULL) {
            strcat(header, rbuf);
            break;
        }
        strcpy(tbuf, rbuf);
        strcat(header, rbuf);
    }
    return SUCCESS;
}

/*
    Fungsi bantuan untuk mencari nilai field dari sebuah string header yang didapat dari fungsi getHeader.
    parameter:
        - header adalah string yang didapat dari fungsi header
        - field adalah nama field yang ingin dicari
    return value:
        0 apabila field ditemukan
        -1 apabila field tidak ditemukan
*/
int getHeaderValue(const char* header, const char* field, char *output) {
    char *tempPtr = strstr(header, field);
    if (tempPtr != NULL) {
        sscanf(tempPtr, "%*s %[^\r\n]", output);
        return 0;
    }
    else return -1;
}

void quit(int sockfd) {
    char buf[10];
    sprintf(buf, "quit\r\n");
    write(sockfd, buf, strlen(buf));
}
