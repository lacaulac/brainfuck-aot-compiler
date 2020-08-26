// BFJITCompiler.cpp : Ce fichier contient la fonction 'main'. L'exécution du programme commence et se termine à cet endroit.
//

#include <iostream>
#include <Windows.h>

typedef void (__cdecl *barebonesBytecode)();
//https://defuse.ca/online-x86-assembler.htm#disassembly Don't forget to check x64
typedef struct {
    void* rdx;
} typeSaveStruct;

//A wrapper around putchar
void __stdcall displayOneCharacter(int charToShow);

//A wrapper around getchar
int __stdcall getOneCharacter();

void preFunctionCall(BYTE** pCurrentPos, typeSaveStruct* saveStruct);
void postFunctionCall(BYTE** pCurrentPos, typeSaveStruct* saveStruct);

int main(int argc, char** argv)
{
    BYTE programMemory[256];
    memset(programMemory, 0, 256);
    BYTE* bytecode = reinterpret_cast<BYTE*>(VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    BYTE* currentPos = bytecode;

    typeSaveStruct saveStruct;
    saveStruct.rdx = NULL;

    void (__stdcall *dispFuncPtr)(int) = displayOneCharacter;
    int (__stdcall *getCFuncPtr)() = getOneCharacter;
    dispFuncPtr('B');
    dispFuncPtr('F');
    dispFuncPtr(':');

    //Let's set rdx to programMemory's address
    *currentPos = 0x48;
    currentPos++;
    *currentPos = 0xBA;
    currentPos++;

    *(BYTE**)(currentPos) = programMemory;
    currentPos += sizeof(BYTE*);

    if (argc != 2)
    {
        printf("Usage: %s <source file>\n", argv[0]);
        return -1;
    }

    FILE* sourceFile;
    fopen_s(&sourceFile, argv[1], "r");
    if (sourceFile == NULL)
    {
        printf("Couldn't open %s.\n", argv[1]);
        return -2;
    }

    char oldChar = 'F';
    char currentReadSourceChar = 'F';
    while (currentReadSourceChar != EOF)
    {
        oldChar = currentReadSourceChar;
        currentReadSourceChar = fgetc(sourceFile);
        bool isAddSubInstruction = currentReadSourceChar == '+' || currentReadSourceChar == '-';
        bool wasAddSubInstruction = oldChar == '+' || oldChar == '-';

        if (isAddSubInstruction)
        {
            if (!wasAddSubInstruction)
            {
                //Entering an "arithmetic" block (even though it might only contain one arithmetic instruction) arithmétique, so we get the value pointed by rdx into rax in order to do the math stuff
                *(DWORD*)(currentPos) = 0x00028B48; //Little-endian reversed of "mov rax, [rdx]"
                currentPos += 3;
            }
        }
        else if(wasAddSubInstruction) //Leaving an "arithmetic" block
        {
            *(DWORD*)(currentPos) = 0x00028948; //Little-endian reversed of "mov [rdx], rax"
            currentPos += 3;
        }

        switch (currentReadSourceChar)
        {
        case '>':
            *(DWORD*)(currentPos) = 0x01C28348; //Little-endian reversed of "add rdx,0x1"
            currentPos += sizeof(DWORD);
            break;
        case '<':
            *(DWORD*)(currentPos) = 0x01EA8348; //Little-endian reversed of "sub rdx,0x1"
            currentPos += sizeof(DWORD);
            break;
        case '+':
            *(DWORD*)(currentPos) = 0x01C08348; //Little-endian reversed of "add rax,0x1"
            currentPos += sizeof(DWORD);
            break;
        case '-':
            *(DWORD*)(currentPos) = 0x01E88348; //Little-endian reversed of "sub rax,0x1"
            currentPos += sizeof(DWORD);
            break;
        case '.':
            preFunctionCall(&currentPos, &saveStruct);

            //We do the function calling stuff
            *(DWORD*)currentPos = 0x000A8B48; //mov rcx, [rdx]
            currentPos += 3;
            *(short*)currentPos = 0xB848; //Beginning of mov rax, ADDRESS
            currentPos += sizeof(short);
            *(void**)currentPos = (void*)dispFuncPtr; //The actual ADDRESS of displayOneCharacter
            currentPos += sizeof(void**);
            *(short*)currentPos = 0xD0FF; //call rax
            currentPos += sizeof(short);

            postFunctionCall(&currentPos, &saveStruct);

            break;
        case ',':
            preFunctionCall(&currentPos, &saveStruct);

            *(short*)currentPos = 0xB848; //Beginning of mov rax, ADDRESS
            currentPos += sizeof(short);
            *(void**)currentPos = (void*)getCFuncPtr; //The actual ADDRESS of getOneCharacter
            currentPos += sizeof(void**);
            *(short*)currentPos = 0xD0FF; //call rax
            currentPos += sizeof(short);

            //Can't keep the return value in rax since it's used by postFunctionCall to restore edx
            *(DWORD*)currentPos = 0x00C28949; //mov r10, rax
            currentPos += 3;

            postFunctionCall(&currentPos, &saveStruct);

            *(DWORD*)currentPos = 0x0012894C; //mov [rdx], r10
            currentPos += 3;

            break;
        case '[':
            break;
        case ']':
            break;
        }
    }

    //Adding a `ret` instruction at the end to get back to executing main
    *currentPos = '\xC3';

    barebonesBytecode bytecodeFunc = reinterpret_cast<barebonesBytecode>(bytecode);
    bytecodeFunc();

    //Execution
    

    fclose(sourceFile);
    VirtualFree(bytecode, NULL, MEM_RELEASE);
    return 0;
}

void __stdcall displayOneCharacter(int charToShow)
{
    putchar(charToShow & 0xFF); //Only keep the least significant byte (because ASCII, duh)
    return;
}

int __stdcall getOneCharacter()
{
    return getchar() & 0xFF;
}

void preFunctionCall(BYTE** pCurrentPos, typeSaveStruct* saveStruct)
{
    //We save RDX beforehand

    **(short**)pCurrentPos = 0xB848; //Beginning of mov rax, ADDRESS
    *pCurrentPos += sizeof(short);
    **(void***)pCurrentPos = (void*)(&saveStruct->rdx); //The actual ADDRESS of saveStruct.rdx
    *pCurrentPos += sizeof(void**);
    **(DWORD**)pCurrentPos = 0x00108948; //mov [rax], rdx
    *pCurrentPos += 3;

    //Let's move the stack around to avoid any bad stuff happening (or else the call wrecks the stack and thus the call stack for some reason)
    **(void***)pCurrentPos = (void*)0x00000000ffec8148; //sub rsp,0xff
    *pCurrentPos += 7;
    **(void***)pCurrentPos = (void*)0x00000000ffed8148; //sub rbp,0xff
    *pCurrentPos += 7;
}

void postFunctionCall(BYTE** pCurrentPos, typeSaveStruct* saveStruct)
{
    //Let's move back the stack in it's normal place
    **(void***)pCurrentPos = (void*)0x00000000ffc48148; //add rsp,0xff
    *pCurrentPos += 7;
    **(void***)pCurrentPos = (void*)0x00000000ffc58148; //add rbp,0xff
    *pCurrentPos += 7;

    //We restore edx
    **pCurrentPos = 0x50; //push rax
    *pCurrentPos += 1;
    **(short**)pCurrentPos = 0xB848; //Beginning of mov rax, ADDRESS
    *pCurrentPos += sizeof(short);
    **(void***)pCurrentPos = (void*)(&saveStruct->rdx); //The actual ADDRESS of saveStruct.rdx
    *pCurrentPos += sizeof(void**);
    **(DWORD**)pCurrentPos = 0x00108B48; //mov rdx, [rax]
    *pCurrentPos += 3;
    **pCurrentPos = 0x58; //pop rax
    *pCurrentPos += 1;
}
