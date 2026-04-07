#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 65536      
#define HEARTBEAT_INT 20       
#define LOG_FILE "hft_audit.log"
#define ID_FILE "next_order_id.txt"
#define CONFIG_FILE ".\\config.ini" 
#define LOG_QUEUE_SIZE 1024

// Frequência do processador para o Cronômetro de Elite
LARGE_INTEGER freq;

typedef struct {
    double bid;
    double ask;
    char resto_da_msg[BUFFER_SIZE];
    int tamanho_resto;
    uint64_t last_msg_time;
    FILE* log_ptr;
    int next_order_id;
    char target_ip[16];
    int target_port;
    char symbol[16];
    int qty;
    double spread_max;
} HFT_Engine;

// Fila circular de strings para o Logger Assíncrono
char log_queue[LOG_QUEUE_SIZE][512];
volatile int log_head = 0;
volatile int log_tail = 0;
CRITICAL_SECTION log_cs;

void carregar_configuracoes(HFT_Engine* eng) {
    GetPrivateProfileStringA("CONNECTION", "IP", "127.0.0.1", eng->target_ip, sizeof(eng->target_ip), CONFIG_FILE);
    eng->target_port = GetPrivateProfileIntA("CONNECTION", "PORT", 7492, CONFIG_FILE);
    GetPrivateProfileStringA("STRATEGY", "SYMBOL", "AAPL", eng->symbol, sizeof(eng->symbol), CONFIG_FILE);
    eng->qty = GetPrivateProfileIntA("STRATEGY", "QUANTITY", 100, CONFIG_FILE);
    char spread_str[16];
    GetPrivateProfileStringA("STRATEGY", "SPREAD_MAX", "0.0105", spread_str, sizeof(spread_str), CONFIG_FILE);
    eng->spread_max = atof(spread_str);
}

void thread_logger_servant(void* arg) {
    HFT_Engine* eng = (HFT_Engine*)arg;
    while (1) {
        if (log_head != log_tail) {
            EnterCriticalSection(&log_cs);
            if (eng->log_ptr) {
                fprintf(eng->log_ptr, "%s\n", log_queue[log_tail]);
                fflush(eng->log_ptr);
            }
            log_tail = (log_tail + 1) % LOG_QUEUE_SIZE;
            LeaveCriticalSection(&log_cs);
        }
        else { Sleep(1); }
    }
}

void logger_send(const char* msg) {
    EnterCriticalSection(&log_cs);
    sprintf(log_queue[log_head], "[%llu] %s", (uint64_t)time(NULL), msg);
    log_head = (log_head + 1) % LOG_QUEUE_SIZE;
    LeaveCriticalSection(&log_cs);
    printf("[%llu] %s\n", (uint64_t)time(NULL), msg);
}

int carregar_proximo_id() {
    FILE* f = fopen(ID_FILE, "r");
    int id = 1000;
    if (f) {
        if (fscanf(f, "%d", &id) != 1) id = 1000;
        fclose(f);
    }
    return id;
}

void salvar_proximo_id(int id) {
    FILE* f = fopen(ID_FILE, "w");
    if (f) { fprintf(f, "%d", id); fclose(f); }
}

void tratar_erro_ib(HFT_Engine* eng, int codigo, const char* msg_erro) {
    char alerta[256];
    sprintf(alerta, "[ALERTA IB] Codigo: %d | Mensagem: %s", codigo, msg_erro);
    logger_send(alerta);
}

void processar_fluxo_ib(HFT_Engine* eng, char* dados_novos, int tamanho_novo, SOCKET s) {
    // CRONÔMETRO INÍCIO (Sincronizado com o pulso da CPU)
    LARGE_INTEGER t1, t2;
    QueryPerformanceCounter(&t1);

    static char buffer_trabalho[BUFFER_SIZE * 2];
    memcpy(buffer_trabalho, eng->resto_da_msg, eng->tamanho_resto);
    memcpy(buffer_trabalho + eng->tamanho_resto, dados_novos, tamanho_novo);
    int total = eng->tamanho_resto + tamanho_novo;

    char* ultimo_delimitador = NULL;
    for (int i = 0; i < total; i++) {
        if (buffer_trabalho[i] == '|') ultimo_delimitador = &buffer_trabalho[i];
    }

    if (!ultimo_delimitador) {
        if (total < BUFFER_SIZE) {
            memcpy(eng->resto_da_msg, buffer_trabalho, total);
            eng->tamanho_resto = total;
        }
        else { eng->tamanho_resto = 0; }
        return;
    }

    int bytes_utilizados = (int)(ultimo_delimitador - buffer_trabalho) + 1;
    char* msg_fatiada = _strdup(buffer_trabalho);
    msg_fatiada[bytes_utilizados] = '\0';

    char* token = strtok(msg_fatiada, "|");
    while (token != NULL) {
        int msgId = atoi(token);
        if (msgId == 1) {
            char* t_type = strtok(NULL, "|");
            char* t_price = strtok(NULL, "|");
            if (t_type && t_price) {
                int type = atoi(t_type);
                double preco = atof(t_price);
                if (type == 1) eng->bid = preco;
                if (type == 2) eng->ask = preco;

                if (eng->bid > 0 && eng->ask > eng->bid) {
                    double spread = eng->ask - eng->bid;
                    if (spread < eng->spread_max) {
                        char ordem[128];
                        sprintf(ordem, "3|%d|%s|%d|BUY|", eng->next_order_id, eng->symbol, eng->qty);
                        send(s, ordem, (int)strlen(ordem), 0);

                        char log_msg[256];
                        sprintf(log_msg, ">>> GATILHO: ID %d | %s | Spd: %.4f", eng->next_order_id, eng->symbol, spread);
                        logger_send(log_msg);

                        eng->next_order_id++;
                        salvar_proximo_id(eng->next_order_id);
                    }
                }
            }
        }
        else if (msgId == 4) {
            char* t_code = strtok(NULL, "|");
            char* t_msg = strtok(NULL, "|");
            if (t_code && t_msg) tratar_erro_ib(eng, atoi(t_code), t_msg);
        }
        token = strtok(NULL, "|");
    }
    free(msg_fatiada);

    eng->tamanho_resto = total - bytes_utilizados;
    if (eng->tamanho_resto > 0) {
        memcpy(eng->resto_da_msg, ultimo_delimitador + 1, eng->tamanho_resto);
    }

    // CRONÔMETRO FIM E LOG DE LATÊNCIA
    QueryPerformanceCounter(&t2);
    double tempo_us = (double)(t2.QuadPart - t1.QuadPart) * 1000000.0 / freq.QuadPart;
    if (tamanho_novo > 0) {
        char latencia_info[128];
        sprintf(latencia_info, "[LATENCIA] %d bytes processados em %.2f us", tamanho_novo, tempo_us);
        logger_send(latencia_info);
    }
}

int main() {
    // Inicialização da frequência de alta precisão
    QueryPerformanceFrequency(&freq);

    HFT_Engine eng = { 0 };
    carregar_configuracoes(&eng);
    eng.log_ptr = fopen(LOG_FILE, "a");
    eng.next_order_id = carregar_proximo_id();
    char rede_raw[BUFFER_SIZE];

    InitializeCriticalSection(&log_cs);
    _beginthread(thread_logger_servant, 0, &eng);

    // Blindagem de Processo (Kernel)
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    SetProcessAffinityMask(GetCurrentProcess(), 0x01);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

    int nodelay = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(int));

    struct sockaddr_in server = { AF_INET, htons(eng.target_port), {.S_un.S_addr = inet_addr(eng.target_ip) } };

    // LOG DE INICIALIZAÇÃO DETALHADO
    char msg_init[256];
    sprintf(msg_init, "=== SISTEMA ONLINE === ID Inicial: %d | Ativo: %s | Qty: %d", eng.next_order_id, eng.symbol, eng.qty);
    logger_send(msg_init);

    if (connect(s, (struct sockaddr*)&server, sizeof(server)) != 0) {
        logger_send("CRITICAL ERROR: IB Gateway Offline.");
        if (eng.log_ptr) fclose(eng.log_ptr); // FECHAMENTO SEGURO
        return 1;
    }

    u_long modo = 1;
    ioctlsocket(s, FIONBIO, &modo);
    eng.last_msg_time = (uint64_t)time(NULL);

    while (1) {
        int n = recv(s, rede_raw, BUFFER_SIZE, 0);
        if (n > 0) {
            eng.last_msg_time = (uint64_t)time(NULL);
            processar_fluxo_ib(&eng, rede_raw, n, s);
        }
        else if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                if ((uint64_t)time(NULL) - eng.last_msg_time > HEARTBEAT_INT) {
                    send(s, "0|PING|", 7, 0);
                    logger_send("Heartbeat (Sinal de Vida) enviado."); // FEEDBACK DE PING
                    eng.last_msg_time = (uint64_t)time(NULL);
                }
                continue;
            }
            logger_send("ERRO: Conexao perdida.");
            break;
        }
        else if (n == 0) {
            logger_send("INFO: Sessao encerrada pelo servidor."); // LOG DE DESCONEXÃO
            break;
        }
    }

    DeleteCriticalSection(&log_cs);
    if (eng.log_ptr) fclose(eng.log_ptr); // FECHAMENTO SEGURO FINAL
    closesocket(s);
    WSACleanup();
    return 0;
}
