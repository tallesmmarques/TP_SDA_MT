// TP_SDA.cpp : Este arquivo cont�m a fun��o 'main'. A execu��o do programa come�a e termina ali.
//

#define _CRT_SECURE_NO_WARNINGS /* Para evitar warnings sobre fun�oes seguras de manipulacao de strings*/
#define _WINSOCK_DEPRECATED_NO_WARNINGS /* para evitar warnings de fun��es WINSOCK depreciadas */

// Para evitar warnings do Visual Studio
#pragma warning(disable:6031)
#pragma warning(disable:6385)


#define WIN32_LEAN_AND_MEAN 
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <conio.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <winsock2.h>
#include <cmath>

#include <atlbase.h>
#include <iostream>
#include <objidl.h>

#include "opcda.h"
#include "opcerror.h"
#include "SOCDataCallback.h"
#include "SOCWrapperFunctions.h"
#include "OPCClient.h"


// Casting para terceiro e sexto par�metros da fun��o _beginthreadex
typedef unsigned (WINAPI* CAST_FUNCTION)(LPVOID);
typedef unsigned* CAST_LPDWORD;

/* tamanho das strings de envio e rebimento */
#define TAMMSGDADOS  29
#define TAMMSGACK     8  
#define TAMMSGREQ     8    
#define TAMMSGACKCP   8  
#define TAMMSGPAR    24 
#define BUFSZ 100

/* configuracoes do console */
#define WHITE   FOREGROUND_RED   | FOREGROUND_GREEN      | FOREGROUND_BLUE  | FOREGROUND_INTENSITY
#define HLGREEN FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define HLRED   FOREGROUND_RED   | FOREGROUND_INTENSITY
#define HLBLUE  FOREGROUND_BLUE  | FOREGROUND_INTENSITY
#define YELLOW  FOREGROUND_RED   | FOREGROUND_GREEN
#define CYAN    FOREGROUND_BLUE  | FOREGROUND_GREEN      | FOREGROUND_INTENSITY

/* teclas de controle */
#define	ESC				0x1B
#define MINUSCULO		0x20
#define	S				0x53

#define OPC_SERVER_NAME L"Matrikon.OPC.Simulation.1"
wchar_t TEMPERATURA_ID[] = L"Random.Real4";
wchar_t UMIDADE_ID[] = L"Saw-toothed Waves.Real4";
wchar_t GRANULOMETRIA_ID[] = L"Triangle Waves.Real4";
wchar_t INCLINACAO_ID[] = L"Bucket Brigade.Real8";
wchar_t VELOCIDADE_ID[] = L"Bucket Brigade.Real4";
wchar_t VAZAO_ID[] = L"Bucket Brigade.Int4";

OPCClient* pOPCClient = nullptr;
int TemperaturaItemID;
int UmidadeItemID;
int GranulometriaItemID;

int InclinacaoItemID;
int VelocidadeItemID;
int VazaoItemID;

WSADATA     wsaData;
SOCKET s;
SOCKADDR_IN servaddr; //endereco do servidor
int status = 0;
int port; //porta de conexao com o servidor
char ip[16];
int nseq_r = 1;
int nseq_e = 1;
int acao;
char buf[BUFSZ];

DWORD dwThreadId;
DWORD dwExitCode = 0;

/* handle para threads */
HANDLE hThreadEnviaMsg;
HANDLE hThreadOPC;
HANDLE hEventS;					// Evento de tecla S
HANDLE hEventESC;				// Evento de tecla ESC
HANDLE hEventSyncWrite;			// Evento de Escrita Sincrona
HANDLE hEventASyncRead;			// Evento de Leitura Assincrona
HANDLE hOut;					// Handle para console

double Temperatura = 0;
double Umidade = 0;
double Granulometria = 0;
double Inclinacao = 0;
float Velocidade = 0;
int Vazao = 0;

/* definicao das threads */
DWORD WINAPI ThreadEnviaMsg(LPVOID);
DWORD WINAPI ThreadOPC(LPVOID);

/* funcao para erro de conexao */
int CheckSocketError(int status) {
	int erro;
	SetConsoleTextAttribute(hOut, HLRED);
	if (status == SOCKET_ERROR) {
		erro = WSAGetLastError();
		if (erro == WSAEWOULDBLOCK) {
			printf("Timeout na operacao de RECV! Erro = %d\n", erro);
			return -1;
		}
		else if (erro == WSAECONNABORTED) {
			printf("Conexao abortada pelo cliente TCP! Erro = %d\n", erro);
			return -1;
		}
		else {
			printf("Erro de conexao! Erro = %d\n", erro);
			return -2;
		}
	} else if (status == 0) {
		printf("Conexao com cliente TCP encerrada prematuramente! Status = %d\n", status);
		return -1;
	}
	else return 0;
}

/* funcao para conexao */
int ServerConnect() {
	DWORD ret;
	int tempo = 0;

	while (TRUE) {
		ret = WaitForSingleObject(hEventESC, tempo);
			// Cria��o do socket
			printf("[Log] Criando socket ...\n");
		if (ret == WAIT_TIMEOUT) { //caso o tempo ate a proxima tentativa se esgote
			s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (s == INVALID_SOCKET) {
				status = WSAGetLastError();
				if (status == WSAENETDOWN) {
					SetConsoleTextAttribute(hOut, HLRED);
					printf("[Err] Rede ou servidor de sockets inacess�veis!\n");
				}
				else {
					SetConsoleTextAttribute(hOut, HLRED);
					printf("[Err] Falha na rede: codigo de erro = %d\n", status);
				}
				return 1;
			}

			// Conec��o com servidor socket
			if (connect(s, (SOCKADDR*)&servaddr, sizeof(servaddr)) != 0) {
				status = WSAGetLastError();
				if (status != WSAECONNREFUSED) {
					SetConsoleTextAttribute(hOut, HLRED);
					printf("Erro na conexao com o servidor. Erro = %d\n", status);
					return 1;
				}
				SetConsoleTextAttribute(hOut, WHITE);
				printf("[Log] Tentando conexao novamente em 5 segundos...\n");
				tempo = 5000;
			}
			else { // conexao bem sucedida
				break;
			}
		}
		else if (ret == WAIT_OBJECT_0) {
			printf("Erro na funcao de conexao... Erro: %d\n", GetLastError());
			return 1;
		}
	}
	return 0;
}


int main(int argc, char** argv)
{
	int nTecla;

	/* Verifica parametros de linha de comando */
	if (argc != 3) {
		printf("parametros de linha de comando invalidos\n");
		_exit(0);
	}
	else if (argc == 3) {
		port = atoi(argv[1]);
		strcpy(ip,argv[2]);
	}

	/* Obt�m um handle para a sa�da da console */
	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE) {
		SetConsoleTextAttribute(hOut, HLRED);
		printf("[Err] Erro ao obter handle para a sa�da da console\n");
	}

	/* Inicializa Winsock vers�o 2.2 */
	status = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (status != 0) {
		SetConsoleTextAttribute(hOut, HLRED);
		printf("[Err] Falha na inicializacao do Winsock 2! Erro  = %d\n", WSAGetLastError());
		WSACleanup();
		exit(0);
	}

	/* Configura endereco do servidor */
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = inet_addr(ip);

	/* Obtem handles */
	hEventS = CreateEvent(NULL, FALSE, FALSE, "EventoS");
	hEventESC = CreateEvent(NULL, FALSE, FALSE, "EventoESC");
	hEventSyncWrite = CreateEvent(NULL, FALSE, FALSE, "EventoSyncWrite");
	hEventASyncRead = CreateEvent(NULL, FALSE, FALSE, "EventoASyncRead");

	// ************************
	// *  CRIA��O DE THREADS  *
	// ************************

	hThreadEnviaMsg = (HANDLE)_beginthreadex(
		NULL,
		0,
		(CAST_FUNCTION)ThreadEnviaMsg,
		(LPVOID) NULL,
		0,
		(CAST_LPDWORD)&dwThreadId
	);
	if (hThreadEnviaMsg) {
		SetConsoleTextAttribute(hOut, WHITE);
		printf("[Log] Thread de Envio das mensagens criada, Id = %0x \n", dwThreadId);
	}

	hThreadOPC = (HANDLE)_beginthreadex(
		NULL,
		0,
		(CAST_FUNCTION)ThreadOPC,
		(LPVOID) NULL,
		0,
		(CAST_LPDWORD)&dwThreadId
	);
	if (hThreadEnviaMsg) {
		SetConsoleTextAttribute(hOut, WHITE);
		printf("[Log] Thread de cliente OPC criada, Id = %0x \n", dwThreadId);
	}

	/* loop de leitura de teclado */
	while (TRUE) {
		nTecla = _getch();
		if (nTecla == S || nTecla == S + MINUSCULO) SetEvent(hEventS); //evendo da tecla s
		if (nTecla == ESC) { //encerramento de programa
			SetEvent(hEventESC);
			SetConsoleTextAttribute(hOut, WHITE);
			printf("Tecla de encerramento do programa ativada...\n");
			break;
		};
	}


	/* ENCERRA PROGRAMA */
	GetExitCodeThread(hThreadEnviaMsg, &dwExitCode);
	CloseHandle(hThreadEnviaMsg);
	GetExitCodeThread(hThreadOPC, &dwExitCode);
	CloseHandle(hThreadOPC);

	CloseHandle(hEventS);
	CloseHandle(hEventESC);
	CloseHandle(hEventSyncWrite);
	CloseHandle(hEventASyncRead);

	closesocket(s);
	WSACleanup();

	SetConsoleTextAttribute(hOut, WHITE);
	printf("Encerrando o programa ...");

	return EXIT_SUCCESS;
}


DWORD WINAPI ThreadEnviaMsg(LPVOID id) {
	DWORD ret;
	char msgdados[TAMMSGDADOS + 2];
	char msgreq[TAMMSGREQ + 1];
	char msgack[TAMMSGACK + 1];
	int desconto = 0;
	int atual = 0;
	HANDLE Events[2] = { hEventS, hEventESC };
	short int aborta = ServerConnect();
	while (!aborta) {
		while (TRUE) {
			atual = GetTickCount64();
			ret = WaitForMultipleObjects(2, Events, FALSE, 2000 - desconto);
			if (ret == WAIT_OBJECT_0) {
				// Envio de mensagem aperiodica
				sprintf_s(msgreq, "%05d$33", nseq_e++);
				status = send(s, msgreq, TAMMSGREQ, 0);
				if ((acao = CheckSocketError(status)) != 0) break;
				SetConsoleTextAttribute(hOut, HLBLUE);
				printf("Mensagem de requisicao de parametros de controle enviada:\n%s\n", msgreq);

				// Recebimento dos dados
				memset(buf, 0, sizeof(buf));
				status = recv(s, buf, TAMMSGPAR, 0);
				if ((acao = CheckSocketError(status)) != 0) break;
				sscanf_s(buf, "%5d", &nseq_r);
				if ((nseq_e++) != nseq_r) {
					// Recebeu numero de mensagem estranho
					SetConsoleTextAttribute(hOut, HLRED);
					printf("Numero de mensagem invalido... Recebido: %d. Esperado: %d\n", nseq_r, nseq_e);
					aborta = 1;
					break;
				}
				if (strncmp(&buf[6], "45", 2) != 0) {
					// Recebeu codigo de mensagem errado
					SetConsoleTextAttribute(hOut, HLRED);
					printf("Codigo de mensagem invalido. Espera-se o codigo 45.\n");
					aborta = 1;
					break;
				}
				SetConsoleTextAttribute(hOut, CYAN);
				printf("Parametros de controle recebidos:\n%s\n", buf);

				// Envio do ACK
				sprintf_s(msgack, "%05d$00", nseq_e++);
				status = send(s, msgack, TAMMSGACK, 0);
				if ((acao = CheckSocketError(status, hOut)) != 0) break;

				SetConsoleTextAttribute(hOut, YELLOW);
				printf("Mensagem ACK enviada:\n%s\n\n", msgack);

				desconto += GetTickCount64() - atual;
				if (desconto > 2000) desconto = 0;

				std::stringstream msg(buf);
				std::string segment;
				std::vector<std::string> seglist;
				while (std::getline(msg, segment, '$')) 
				{
					seglist.push_back(segment);
				}

				Inclinacao = stod(seglist.at(2));
				Velocidade = stof(seglist.at(3));
				Vazao = stoi(seglist.at(4));
				SetEvent(hEventSyncWrite);
			}
			else if (ret == WAIT_TIMEOUT) {
				// Mensagem periodica
				sprintf_s(msgdados, "%05d$55$%07.1f$%06.1f$%05.1f\n", nseq_e++, 
					fmod(Temperatura, 100000), fmod(Umidade, 10000), fmod(Granulometria, 1000));
				status = send(s, msgdados, TAMMSGDADOS, 0);
				if ((acao = CheckSocketError(status)) != 0) break;
				SetConsoleTextAttribute(hOut, HLGREEN);
				printf("Mensagem enviada do CLP do disco de pelotamento:\n%s\n", msgdados);

				// Recebimento do ACK
				memset(buf, 0, sizeof(buf));
				status = recv(s, buf, TAMMSGACKCP, 0);
				if ((acao = CheckSocketError(status)) != 0) break;
				sscanf_s(buf, "%5d", &nseq_r);
				if ((nseq_e++) != nseq_r) {
					//recebeu numero de mensagem errado
					SetConsoleTextAttribute(hOut, HLRED);
					printf("Numero de mensagem invalido... Recebido: %d. Esperado: %d\n", nseq_r, nseq_e);
					aborta = 1;
					break;
				}
				if (strncmp(&buf[6], "99", 2) != 0) {
					// Recebeu codigo de mensagem errado
					SetConsoleTextAttribute(hOut, HLRED);
					printf("Codigo de mensagem invalido. Espera-se o codigo 99.\n");
					aborta = 1;
					break;
				}
				SetConsoleTextAttribute(hOut, YELLOW);
				printf("Mensagem ACK recebida para o CLP do disco de pelotamento:\n%s\n", buf);
				desconto = 0;
			}
			else if (ret == WAIT_OBJECT_0 + 1) { //tecla ESC pressionada
				aborta = 1;
				break;
			}
			else { //
				SetConsoleTextAttribute(hOut, HLRED);
				printf("Erro na thread de leitura de dados de processo. Erro: %d\n", GetLastError());
				aborta = 1;
				break;
			}
		}

		if (acao == -2) {
			aborta = ServerConnect();
		} else break;
	}
	//encerra programa caso "aborta = 1" pois houve algum erro
	_endthreadex(0);
	return 0;
}


DWORD WINAPI ThreadOPC(LPVOID id) {
	HRESULT hr;

	SetConsoleTextAttribute(hOut, WHITE);
	printf("[Log] Inicializando ambiente COM...\n");
	hr = OPCClient::StartupCOM();
	_ASSERT(!FAILED(hr));

	OPCClient* pOPCClient = new OPCClient;

	SetConsoleTextAttribute(hOut, WHITE);
	printf("[Log] Instanciando servidor OPC...\n");
	pOPCClient->InstantiateServer();

	SetConsoleTextAttribute(hOut, WHITE);
	printf("[Log] Adicionando grupo ao servidor OPC...\n");
	pOPCClient->AddGroup(L"Group1");

	SetConsoleTextAttribute(hOut, WHITE);
	printf("[Log] Adicionando itens ao grupo do servidor OPC...\n");
	TemperaturaItemID = pOPCClient->AddItem(TEMPERATURA_ID, VT_R4);
	UmidadeItemID = pOPCClient->AddItem(UMIDADE_ID, VT_R4);
	GranulometriaItemID = pOPCClient->AddItem(GRANULOMETRIA_ID, VT_R4);
	InclinacaoItemID = pOPCClient->AddItem(INCLINACAO_ID, VT_R8);
	VelocidadeItemID = pOPCClient->AddItem(VELOCIDADE_ID, VT_R4);
	VazaoItemID = pOPCClient->AddItem(VAZAO_ID, VT_I4);

	int ItemsID[3] = { InclinacaoItemID, VelocidadeItemID, VazaoItemID };

	pOPCClient->StartupASyncRead();

	int bRet;
	MSG msg;
	DWORD ret;
	while (TRUE)
	{
		DWORD ret = WaitForSingleObject(hEventSyncWrite, 10);
		if (ret == WAIT_OBJECT_0)
		{
			pOPCClient->SyncWriteItem(ItemsID, Inclinacao, Velocidade, Vazao);
		}
		else if (ret == WAIT_TIMEOUT) {
			bRet = GetMessage( &msg, NULL, 0, 0 );
			if (!bRet) {
				printf("[Err] Falha ao receber mensagem! Codigo de erro = %d\n", GetLastError());
				exit(0);
			}
			
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			Temperatura = pOPCClient->GetASyncReadItem(TemperaturaItemID);
			Umidade = pOPCClient->GetASyncReadItem(UmidadeItemID);
			Granulometria = pOPCClient->GetASyncReadItem(GranulometriaItemID);
		}
	}

	pOPCClient->CancelASyncRead();

	SetConsoleTextAttribute(hOut, WHITE);
	printf("[End] Finalizando instancias...\n");
	delete pOPCClient;

	SetConsoleTextAttribute(hOut, WHITE);
	printf("[End] Finalizando ambiente COM...\n");
	OPCClient::ReleaseCOM();

	SetConsoleTextAttribute(hOut, WHITE);
	printf("[End] Encerrando a thread OPC ...\n");
	_endthreadex(0);
	return 0;
}

