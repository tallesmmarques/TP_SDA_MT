#include <atlbase.h>
#include "opcerror.h"
#include "OPCClient.h"
#include "SOCWrapperFunctions.h"

HRESULT OPCClient::StartupCOM() { return CoInitialize(NULL); }

void OPCClient::ReleaseCOM() { CoUninitialize(); } 

void OPCClient::InstantiateServer()
{
	CLSID CLSID_OPCServer;
	HRESULT hr;

	// Recebe o CLSID a partir do nome do servidor
	hr = CLSIDFromString(OPC_SERVER_NAME, &CLSID_OPCServer);
	_ASSERT(!FAILED(hr));


	LONG const cmp = 1;
	// Tipo de instancia a ser criada
	MULTI_QI queue[cmp] = { {&IID_IOPCServer, NULL, 0} };

	// Instancia objetos dos tipos definidos em queue
	hr = CoCreateInstanceEx(CLSID_OPCServer, NULL, CLSCTX_SERVER,
		NULL, cmp, queue);
	_ASSERT(!FAILED(hr));

	pIOPCServer = (IOPCServer*)queue[0].pItf;
}

void OPCClient::AddGroup(LPCWSTR name)
{
	DWORD dwUpdateRate = 0;

	HRESULT hr = pIOPCServer->AddGroup(
		/*szName*/ name,
		/*bActive*/ FALSE,
		/*dwRequestedUpdateRate*/ 1000,
		/*hClientGroup*/ hClientGroup,
		/*pTimeBias*/ 0,
		/*pPercentDeadband*/ 0,
		/*dwLCID*/0,
		/*phServerGroup*/&hServerGroup,
		&dwUpdateRate,
		/*riid*/ IID_IOPCItemMgt,
		/*ppUnk*/ (IUnknown**) &pIOPCItemMgt);
	_ASSERT(!FAILED(hr));
}

int OPCClient::AddItem(wchar_t item_id[], enum VARENUM VT)
{
	LONG const cmp = 1;
	OPCITEMDEF ItemArray[cmp] = { {
			/*szAccessPath*/ L"",
			/*szItemID*/ item_id,
			/*bActive*/ TRUE,
			/*hClient*/ _numItems,
			/*dwBlobSize*/ 0,
			/*pBlob*/ NULL,
			/*vtRequestedDataType*/ VT,
			/*wReserved*/0
		} };

	OPCITEMRESULT* pAddResult = nullptr;
	HRESULT* pErrors = nullptr;

	if (pIOPCItemMgt == nullptr)
	{
		printf("Grupo ainda não adicionado!\n");
		exit(EXIT_FAILURE);
	}

	HRESULT hr = pIOPCItemMgt->AddItems(cmp, ItemArray, &pAddResult, &pErrors);
	if (hr != S_OK)
	{
		printf("Falha na chamada da função AddItems. Código de erro = %x\n", hr);
		exit(EXIT_FAILURE);
	}

	hServerItems.push_back(pAddResult[0].hServer);
	ItemsValue.push_back(0);
	return _numItems++;
}

double OPCClient::SyncReadItem(int item_num_id)
{
	VARIANT varValue;
	VariantInit(&varValue);

	OPCITEMSTATE* pValue = nullptr;
	HRESULT* pErrors = nullptr;
	IOPCSyncIO* pIOPCSyncIO;

	pIOPCItemMgt->QueryInterface(__uuidof(pIOPCSyncIO), (void**)&pIOPCSyncIO);

	HRESULT hr = pIOPCSyncIO->Read(OPC_DS_DEVICE,
		1, &hServerItems.at(item_num_id), &pValue, &pErrors);
	_ASSERT(!FAILED(hr));
	_ASSERT(pValue != NULL);
	
	varValue = pValue[0].vDataValue;

	CoTaskMemFree(pErrors);
	pErrors = nullptr;

	CoTaskMemFree(pValue);
	pValue = nullptr;

	pIOPCSyncIO->Release();

	return varValue.fltVal;
}

void OPCClient::SyncWriteItem(int item_num_id, double value, enum VARENUM VT)
{
	HRESULT hr;
	VARIANT varValue;
	VariantInit(&varValue);
	varValue.fltVal = value;
	varValue.vt = VT;

	HRESULT* pErrors = nullptr;

	if (pIOPCSyncIO == nullptr)
	{
		hr = pIOPCItemMgt->QueryInterface(__uuidof(pIOPCSyncIO), (void**)&pIOPCSyncIO);
		_ASSERT(!FAILED(hr));
	}

	hr = pIOPCSyncIO->Write(1, &hServerItems.at(item_num_id),
		&varValue, &pErrors);
	_ASSERT(!FAILED(hr));
	_ASSERT(!FAILED(pErrors));

	CoTaskMemFree(pErrors);
	pErrors = nullptr;
}

void OPCClient::SyncWriteItem(int item_num_id[3], double value1, float value2, int value3)
{
	HRESULT hr;
	VARIANT varValue[3];
	VariantInit(varValue);

	varValue[0].dblVal = value1;
	varValue[0].vt = VT_R8;

	varValue[1].fltVal = value2;
	varValue[1].vt = VT_R4;

	varValue[2].intVal = value3;
	varValue[2].vt = VT_I4;

	HRESULT* pErrors = nullptr;

	if (pIOPCSyncIO == nullptr)
	{
		hr = pIOPCItemMgt->QueryInterface(__uuidof(pIOPCSyncIO), (void**)&pIOPCSyncIO);
		_ASSERT(!FAILED(hr));
	}

	OPCHANDLE hServerItemsArray[3] = {
		hServerItems.at(item_num_id[0]),
		hServerItems.at(item_num_id[1]),
		hServerItems.at(item_num_id[2]),
	};
	hr = pIOPCSyncIO->Write(3, hServerItemsArray,
		varValue, &pErrors);
	_ASSERT(!FAILED(hr));
	_ASSERT(!FAILED(pErrors));

	CoTaskMemFree(pErrors);
	pErrors = nullptr;
}

void OPCClient::StartupASyncRead()
{
	pSOCDataCallback = new SOCDataCallback();
	pSOCDataCallback->AddRef();
	pSOCDataCallback->SavePointers(&ItemsValue, &hServerItems);

	SetDataCallback(pIOPCItemMgt, pSOCDataCallback, pIConnectionPoint, &dwCookie);
    SetGroupActive(pIOPCItemMgt); 
}

void OPCClient::SaveASyncReadItem(int item_num_id, double value)
{
	ItemsValue.at(item_num_id) = value;
}

double OPCClient::GetASyncReadItem(int item_num_id)
{
	return ItemsValue.at(item_num_id);
}

void OPCClient::CancelASyncRead()
{
    CancelDataCallback(pIConnectionPoint, dwCookie);
	pIConnectionPoint->Release();
	pSOCDataCallback->Release();
}

OPCClient::~OPCClient()
{
	HRESULT hr;
	HRESULT* pErros = nullptr;

	if (pIOPCSyncIO != nullptr)
		pIOPCSyncIO->Release();

	// Remove the OPC items
	for (OPCHANDLE hItem : hServerItems)
	{
		hr = pIOPCItemMgt->RemoveItems(1, &hItem, &pErros);
		_ASSERT(!FAILED(hr));
	}

	// Remove the OPC group object
	pIOPCItemMgt->Release();
	hr = pIOPCServer->RemoveGroup(hServerGroup, FALSE);
	if (hr != S_OK)
	{
		if (hr == OPC_S_INUSE)
			printf("Falha ao remover grupo: objeto ainda possui referências.\n");
		else printf("Falha ao remover group. Código de erro = %x\n", hr);
		exit(EXIT_FAILURE);
	}

	// Remove the OPC server object
	pIOPCServer->Release();
}



