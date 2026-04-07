#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 7492

// Função de Fragmentação Maliciosa (Corta em pontos aleatórios, não apenas no meio)
void send_malicioso(SOCKET s, const char* msg) {
    int len = (int)strlen(msg);
    if (len < 3) { send(s, msg, len, 0); return; }

    int corte = rand() % (len - 1) + 1; // Ponto de corte aleatório
    send(s, msg, corte, 0);
    Sleep(1); // Força o Windows a enviar o pacote incompleto
    send(s, msg + corte, len - corte, 0);
}

int main() {
    WSADATA wsa;
    SOCKET s, client;
    struct sockaddr_in server, c_addr;
    int addr_len = sizeof(c_addr);

    WSAStartup(MAKEWORD(2, 2), &wsa);
    s = socket(AF_INET, SOCK_STREAM, 0);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    bind(s, (struct sockaddr*)&server, sizeof(server));
    listen(s, 1);

    printf("[SIMULADOR HIBRIDO] Aguardando o Robo Elite na porta %d...\n", PORT);
    client = accept(s, (struct sockaddr*)&c_addr, &addr_len);

    // Handshake
    char auth_buf[64] = { 0 };
    recv(client, auth_buf, sizeof(auth_buf), 0);
    send(client, "100|OK|SESSAO_ATIVA|", 20, 0);

    double bid = 150.00;
    char msg[512];
    srand((unsigned int)time(NULL));

    printf("[MODO COMBATE] Iniciando rajadas de estresse e ruído...\n");

    while (1) {
        bid += ((rand() % 3) - 1) * 0.01;
        double ask = bid + 0.01;

        // CENÁRIO 1: Mensagens Coladas e Fragmentadas (Testa o Motor de Parse + Resto_da_Msg)
        if (rand() % 5 == 0) {
            sprintf(msg, "1|1|%.2f|1|2|%.2f|1|1|", bid, ask); // Termina cortado no "1|1|"
            send(client, msg, (int)strlen(msg), 0);
            Sleep(5);
            sprintf(msg, "%.2f|", bid + 0.01); // Completa o preço anterior
            send(client, msg, (int)strlen(msg), 0);
        }
        else {
            sprintf(msg, "1|1|%.2f|1|2|%.2f|", bid, ask);
            send_malicioso(client, msg);
        }

        // CENÁRIO 2: Mensagens de Erro da IB (Testa o tratar_erro_ib e Log Assíncrono)
        if (rand() % 15 == 0) {
            send(client, "4|200|ALERTA: Margem de seguranca atingida na corretora|", 56, 0);
        }

        // CENÁRIO 3: Lixo de Rede e Mensagens Inúteis (Testa a Blindagem do strtok)
        if (rand() % 10 == 0) {
            send(client, "99|DADO_VOLUMETRICO_IGNORADO_PELO_ENGINE|", 41, 0);
        }

        // CENÁRIO 4: Heartbeat PONG (Testa a resiliência do loop de recebimento)
        if (rand() % 20 == 0) {
            send(client, "0|PONG|", 7, 0);
        }

        // CENÁRIO 5: Rajada de Trading (Testa se a Thread de Log trava o Trade)
        if (rand() % 25 == 0) {
            printf("[BURST] Enviando 50 pacotes instantaneos...\n");
            for (int i = 0; i < 50; i++) {
                sprintf(msg, "1|1|%.2f|1|2|%.2f|", bid, ask);
                send(client, msg, (int)strlen(msg), 0);
            }
        }

        Sleep(rand() % 15 + 1);
    }

    closesocket(client);
    WSACleanup();
    return 0;
}
