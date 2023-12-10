// TP_SDA.cpp : Este arquivo cont�m a fun��o 'main'. A execu��o do programa come�a e termina ali.
//


#define _CRT_SECURE_NO_WARNINGS /* Para evitar warnings sobre fun�oes seguras de manipulacao de strings*/
#define _WINSOCK_DEPRECATED_NO_WARNINGS /* para evitar warnings de fun��es WINSOCK depreciadas */

// Para evitar warnings irritantes do Visual Studio
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


#define TAMMSGDADOS  29  // 5+2+7+6+5 caracteres + 4 separadores
#define TAMMSGACK     8  // 5+2 caracteres + 1 separador
#define TAMMSGREQ     8  // 5+2 caracteres + 1 separador    
#define TAMMSGACKCP   8  // 5+2 caracteres + 1 separador
#define TAMMSGPAR    24  // 5+2+4+5+4 caracteres + 4 separadores
#define BUFSZ 100


#define WHITE   FOREGROUND_RED   | FOREGROUND_GREEN      | FOREGROUND_BLUE  | FOREGROUND_INTENSITY
#define HLGREEN FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define HLRED   FOREGROUND_RED   | FOREGROUND_INTENSITY
#define HLBLUE  FOREGROUND_BLUE  | FOREGROUND_INTENSITY
#define YELLOW  FOREGROUND_RED   | FOREGROUND_GREEN
#define CYAN    FOREGROUND_BLUE  | FOREGROUND_GREEN      | FOREGROUND_INTENSITY

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
int port = 2342; //porta de conexao com o servidor
int nseq_r = 1;
int nseq_e = 1;
int acao;

char buf[BUFSZ];


DWORD dwThreadId;
DWORD dwExitCode = 0;
HANDLE hThreadEnviaMsg;
HANDLE hThreadOPC;
HANDLE hThreadOPCCallback;
HANDLE hEventS;					// Evento de tecla S
HANDLE hEventESC;				// Evento de tecla ESC
HANDLE hEventSyncWrite;			// Evento de Escrita Sincrona
HANDLE hEventASyncRead;			// Evento de Leitura Assincrona
HANDLE hOut;

double Temperatura = 0;
double Umidade = 0;
double Granulometria = 0;
double Inclinacao = 0;
float Velocidade = 0;
int Vazao = 0;

DWORD WINAPI thread_envia_msg(LPVOID);
DWORD WINAPI thread_opc(LPVOID);

int CheckSocketError(int status, HANDLE hOut) {
	int erro;

	if (status == SOCKET_ERROR) {
		erro = WSAGetLastError();
		SetConsoleTextAttribute(hOut, HLRED);
		if (erro == WSAEWOULDBLOCK) {
			printf("Timeout na operacao de RECV! errno = %d - reiniciando...\n\n", erro);
			return(-1); // acarreta rein�cio da espera de mensagens no programa principal
		}
		else if (erro == WSAECONNABORTED) {
			printf("Conexao abortada pelo cliente TCP - reiniciando...\n\n");
			return(-1); // acarreta rein�cio da espera de mensagens no programa principal
		}
		else {
			printf("Erro de conexao! valor = %d\n\n", erro);
			return (-2); // acarreta encerramento do programa principal
		}
	}
	else if (status == 0) {
		//Este caso indica que a conex�o foi encerrada suavemente ("gracefully")
		printf("Conexao com cliente TCP encerrada prematuramente! status = %d\n\n", status);
		return(-1); // acarreta rein�cio da espera de mensagens no programa principal
	}
	else return(0);
}

int serverConnect() {
	DWORD ret;
	int tempo = 0;
	while (1) {
		ret = WaitForSingleObject(hEventESC, tempo);
		if (ret == WAIT_TIMEOUT) {
			/* Cria Socket */
			printf("Criando socket ...\n");
			s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (s == INVALID_SOCKET) {
				status = WSAGetLastError();
				if (status == WSAENETDOWN) {
					SetConsoleTextAttribute(hOut, HLRED);
					printf("Rede ou servidor de sockets inacess�veis!\n");
				}
				else {
					SetConsoleTextAttribute(hOut, HLRED);
					printf("Falha na rede: codigo de erro = %d\n", status);
				}
				WSACleanup();
				exit(0);
			}

			/* Conecta com o servidor */
			if (connect(s, (SOCKADDR*)&servaddr, sizeof(servaddr)) != 0) {
				status = WSAGetLastError();
				if (status != WSAECONNREFUSED) {
					SetConsoleTextAttribute(hOut, HLRED);
					printf("Erro na conexao com o servidor. Erro = %d\n", status);
					WSACleanup();
					exit(0);
				}
				SetConsoleTextAttribute(hOut, WHITE);
				printf("Tentando conexao novamente em 5 segundos...\n");
				tempo = 5000;
			}
			else {
				break;
			}
		} else if (ret == WAIT_OBJECT_0) {
			return 1;
		}
	}
	return 0;
}


int main()
{
	//short int i = 0;
	// Obt�m um handle para a sa�da da console
	hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE) {
		SetConsoleTextAttribute(hOut, HLRED);
		printf("Erro ao obter handle para a sa�da da console\n");
	}

	int nTecla;

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	///													FALTA O ENDERECO IP


	/* Inicializa Winsock vers�o 2.2 */
	status = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (status != 0) {
		SetConsoleTextAttribute(hOut, HLRED);
		printf("Falha na inicializacao do Winsock 2! Erro  = %d\n", WSAGetLastError());
		WSACleanup();
		exit(0);
	}


	hEventS = CreateEvent(NULL, FALSE, FALSE, "EventoS");
	hEventESC = CreateEvent(NULL, FALSE, FALSE, "EventoESC");
	hEventSyncWrite = CreateEvent(NULL, FALSE, FALSE, "EventoSyncWrite");
	hEventASyncRead = CreateEvent(NULL, FALSE, FALSE, "EventoASyncRead");
	//CheckForError(hEventS);

	// ************************
	// *  CRIA��O DE THREADS  *
	// ************************

	hThreadEnviaMsg = (HANDLE)_beginthreadex(
		NULL,
		0,
		(CAST_FUNCTION)thread_envia_msg,	// casting necess�rio
		(LPVOID) NULL,
		0,
		(CAST_LPDWORD)&dwThreadId					// casting necess�rio
	);
	if (hThreadEnviaMsg) {
		SetConsoleTextAttribute(hOut, WHITE);
		printf("Thread criada Id = %0x \n", dwThreadId);
	}

	hThreadOPC = (HANDLE)_beginthreadex(
		NULL,
		0,
		(CAST_FUNCTION)thread_opc,	// casting necess�rio
		(LPVOID) NULL,
		0,
		(CAST_LPDWORD)&dwThreadId					// casting necess�rio
	);
	if (hThreadEnviaMsg) {
		SetConsoleTextAttribute(hOut, WHITE);
		printf("Thread criada Id = %0x \n", dwThreadId);
	}


	while (TRUE) {
		//verifica teclado
		nTecla = _getch();
		if (nTecla == S || nTecla == S + MINUSCULO) SetEvent(hEventS);
		if (nTecla == ESC) {
			SetEvent(hEventESC);
			break;
		};
	}


	GetExitCodeThread(hThreadEnviaMsg, &dwExitCode);
	CloseHandle(hThreadEnviaMsg);
	GetExitCodeThread(hThreadOPC, &dwExitCode);
	CloseHandle(hThreadOPC);

	CloseHandle(hEventS);
	CloseHandle(hEventESC);
	CloseHandle(hEventSyncWrite);
	CloseHandle(hEventASyncRead);

	return EXIT_SUCCESS;
}


DWORD WINAPI thread_envia_msg(LPVOID id) {
	DWORD ret;
	char msgdados[TAMMSGDADOS + 2];
	char msgreq[TAMMSGREQ + 1];
	char msgack[TAMMSGACK + 1];
	int desconto = 0;
	int atual = 0;
	HANDLE Events[2] = { hEventS, hEventESC };
	short int aborta = serverConnect();
	while (!aborta) {
		while (TRUE) {
			atual = GetTickCount64();
			ret = WaitForMultipleObjects(2, Events, FALSE, 2000 - desconto);
			if (ret == WAIT_OBJECT_0) {
				//envia mensagem aperiodica
				sprintf_s(msgreq, "%05d$33", nseq_e++);
				status = send(s, msgreq, TAMMSGREQ, 0);
				if ((acao = CheckSocketError(status, hOut)) != 0) break;
				SetConsoleTextAttribute(hOut, HLBLUE);
				printf("Mensagem de requisicao de parametros de controle enviada:\n%s\n", msgreq);
				//recebe dados
				memset(buf, 0, sizeof(buf));
				status = recv(s, buf, TAMMSGPAR, 0);
				if ((acao = CheckSocketError(status, hOut)) != 0) break;
				sscanf_s(buf, "%5d", &nseq_r);
				if ((nseq_e++) != nseq_r) {
					//recebeu numero de mensagem esquisito
					SetConsoleTextAttribute(hOut, HLRED);
					printf("recebeu numero de mensagem esquisito: %s\n", buf);
					WSACleanup();
					exit(0);
				}
				if (strncmp(&buf[6], "45", 2) != 0) {
					//recebeu codigo de mensagem errado
					SetConsoleTextAttribute(hOut, HLRED);
					printf("recebeu codigo de mensagem errado:%s\n", &buf[6]);
					WSACleanup();
					exit(0);
				}
				SetConsoleTextAttribute(hOut, CYAN);
				printf("Parametros de controle recebidos:\n%s\n", buf);

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

				printf("inclinacao: %f, velocidade: %f, vazao: %d\n", Inclinacao, Velocidade, Vazao);
				SetEvent(hEventSyncWrite);
				//pOPCClient->SyncWriteItem(VelocidadeItemID, velocidade);
				//pOPCClient->SyncWriteItem(VelocidadeItemID, velocidade);
				//pOPCClient->SyncWriteItem(VazaoItemID, vazao);

				//envia ack
				sprintf_s(msgack, "%05d$00", nseq_e++);
				status = send(s, msgack, TAMMSGACK, 0);
				if ((acao = CheckSocketError(status, hOut)) != 0) break;
				SetConsoleTextAttribute(hOut, YELLOW);
				printf("Mensagem ACK enviada:\n%s\n\n", msgack);
				desconto += GetTickCount64() - atual;
				if (desconto > 2000) desconto = 0;
			}
			else if (ret == WAIT_TIMEOUT) {
				//envia mensagem periodica
				//SetConsoleTextAttribute(hOut, WHITE);
				//printf("%05d$55$%07.1f$%06.1f$%05.1f\n", nseq_e,
				//	fmod(Temperatura, 100000), fmod(Umidade, 10000), fmod(Granulometria, 1000));

				//sprintf_s(msgdados, "%05d$55$NNNNN.N$NNNN.N$NNN.N", nseq_e++);
				sprintf_s(msgdados, "%05d$55$%07.1f$%06.1f$%05.1f\n", nseq_e++, 
					fmod(Temperatura, 100000), fmod(Umidade, 10000), fmod(Granulometria, 1000));
				status = send(s, msgdados, TAMMSGDADOS, 0);
				if ((acao = CheckSocketError(status, hOut)) != 0) break;
				SetConsoleTextAttribute(hOut, HLGREEN);
				printf("Mensagem enviada do CLP do disco de pelotamento:\n%s\n", msgdados);
				//recebe ack
				memset(buf, 0, sizeof(buf));
				status = recv(s, buf, TAMMSGACKCP, 0);
				if ((acao = CheckSocketError(status, hOut)) != 0) break;
				sscanf_s(buf, "%5d", &nseq_r);
				if ((nseq_e++) != nseq_r) {
					//recebeu numero de mensagem esquisito
					SetConsoleTextAttribute(hOut, HLRED);
					printf("recebeu numero de mensagem esquisito: %s\n", buf);
					WSACleanup();
					exit(0);
				}
				if (strncmp(&buf[6], "99", 2) != 0) {
					//recebeu codigo de mensagem errado
					SetConsoleTextAttribute(hOut, HLRED);
					printf("recebeu codigo de mensagem errado:%s\n", &buf[6]);
					WSACleanup();
					exit(0);
				}
				SetConsoleTextAttribute(hOut, YELLOW);
				printf("Mensagem ACK recebida para o CLP do disco de pelotamento:\n%s\n\n", buf);
				desconto = 0;
			}
			else if (ret == WAIT_OBJECT_0 + 1) {
				SetConsoleTextAttribute(hOut, WHITE);
				printf("Tecla de enceramento do programa ativada...\n");
				acao = -1;
				break;
			}
			else {
				SetConsoleTextAttribute(hOut, HLRED);
				printf("Erro na thread de leitura de dados de processo. Erro: %d\n", GetLastError()); //retorno ruim
			}
		}
		if (acao == -2) {
			aborta = serverConnect();
		} else {
			break;
		}
	}
	closesocket(s);
	WSACleanup();
	SetConsoleTextAttribute(hOut, WHITE);
	printf("Encerrando o programa ...");
	_endthreadex(0);
	return 0;
}

DWORD WINAPI thread_opc(LPVOID id) {
	HRESULT hr;

	printf("Inicializando ambiente COM...\n");
	hr = OPCClient::StartupCOM();
	_ASSERT(!FAILED(hr));

	OPCClient* pOPCClient = new OPCClient;

	printf("Instanciando servidor OPC...\n");
	pOPCClient->InstantiateServer();

	printf("Adicionando grupo ao servidor OPC...\n");
	pOPCClient->AddGroup(L"Group1");

	printf("Adicionando itens ao grupo do servidor OPC...\n");
	TemperaturaItemID = pOPCClient->AddItem(TEMPERATURA_ID, VT_R4);
	UmidadeItemID = pOPCClient->AddItem(UMIDADE_ID, VT_R4);
	GranulometriaItemID = pOPCClient->AddItem(GRANULOMETRIA_ID, VT_R4);

	InclinacaoItemID = pOPCClient->AddItem(INCLINACAO_ID, VT_R8);
	VelocidadeItemID = pOPCClient->AddItem(VELOCIDADE_ID, VT_R4);
	VazaoItemID = pOPCClient->AddItem(VAZAO_ID, VT_I4);

	printf("Leitura 1: %f\n", pOPCClient->SyncReadItem(TemperaturaItemID));
	printf("Leitura 2: %f\n", pOPCClient->SyncReadItem(UmidadeItemID));
	printf("Leitura 3: %f\n", pOPCClient->SyncReadItem(GranulometriaItemID));

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
			if (!bRet){
				printf ("Failed to get windows message! Error code = %d\n", GetLastError());
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

	printf("Finalizando instancias...\n");
	delete pOPCClient;

	printf("Finalizando ambiente COM...\n");
	OPCClient::ReleaseCOM();

	SetConsoleTextAttribute(hOut, WHITE);
	printf("Encerrando a thread OPC ...\n");
	_endthreadex(0);
	return 0;
}
