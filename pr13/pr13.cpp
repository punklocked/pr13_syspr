#include <iostream>
#include <Windows.h>

using namespace std;

struct Bayum {
    long long health = 9000000000;
    int resist = 44;
    int damage = 73843;
    int specialDamage = 150000;
    int attackCooldown = 5;
    int specialCooldown = 10;
};

struct Player {
    long health = 500000;
    int damage = 12000;
    int specialDamage = 30000;
    int defense = 20;
    int dodgeChance = 15;
    int attackCooldown = 2;
    int specialCooldown = 5;
    char name[64];

    int dealtDamage = 0;
    bool isDead = false;
    DWORD lastSpecialTime = 0;
    DWORD nextAttackTime = 0;
};

Bayum bayum;
Player players[10];

int countPlayers;
int alivePlayers;
int currentTurn = 0;

CRITICAL_SECTION cs;

HANDLE hPlayerTurnEvents[10];
HANDLE hBossSpecialEvent;
HANDLE hConsole;

int rnd(int a, int b) {
    return a + rand() % (b - a + 1);
}

void setColor(int c) {
    SetConsoleTextAttribute(hConsole, c);
}

int findNextAlivePlayerLocked(int startIndex) {
    if (alivePlayers <= 0) return -1;

    for (int step = 1; step <= countPlayers; step++) {
        int index = (startIndex + step) % countPlayers;
        if (!players[index].isDead && players[index].health > 0) {
            return index;
        }
    }

    return -1;
}

void markPlayerDeadLocked(int id) {
    if (players[id].isDead || players[id].health > 0) return;

    players[id].isDead = true;
    alivePlayers--;

    setColor(12);
    cout << players[id].name << " погиб\n";

    if (alivePlayers > 0 && currentTurn == id) {
        int nextPlayer = findNextAlivePlayerLocked(id);
        if (nextPlayer != -1) {
            currentTurn = nextPlayer;
        }
    }
}

void sortPlayers() {
    for (int i = 0; i < countPlayers - 1; i++) {
        for (int j = 0; j < countPlayers - i - 1; j++) {
            if (players[j].dealtDamage < players[j + 1].dealtDamage) {
                swap(players[j], players[j + 1]);
            }
        }
    }
}

DWORD WINAPI BossThread(LPVOID) {
    while (true) {
        Sleep(bayum.attackCooldown * 1000);

        EnterCriticalSection(&cs);

        if (bayum.health <= 0 || alivePlayers <= 0) {
            LeaveCriticalSection(&cs);
            return 0;
        }

        int oldTurn = currentTurn;
        int target;
        do {
            target = rnd(0, countPlayers - 1);
        } while (players[target].isDead);

        Player& p = players[target];

        if (rnd(1, 100) <= p.dodgeChance) {
            setColor(10);
            cout << p.name << " увернулся\n";
        }
        else {
            int dmg = bayum.damage * (100 - p.defense) / 100;
            p.health -= dmg;
            if (p.health < 0) p.health = 0;

            setColor(12);
            cout << "Босс ударил " << p.name
                << " на " << dmg
                << " | HP: " << p.health << endl;

            markPlayerDeadLocked(target);
        }

        int nextTurn = -1;
        if (alivePlayers > 0 && oldTurn != currentTurn) {
            nextTurn = currentTurn;
        }

        LeaveCriticalSection(&cs);

        if (nextTurn != -1) {
            SetEvent(hPlayerTurnEvents[nextTurn]);
        }
    }
}

DWORD WINAPI BossSpecial(LPVOID) {
    while (true) {
        Sleep(bayum.specialCooldown * 1000);

        EnterCriticalSection(&cs);

        if (bayum.health <= 0 || alivePlayers <= 0) {
            LeaveCriticalSection(&cs);
            return 0;
        }

        setColor(13);
        cout << "Босс использует СПЕЦ атаку!" << endl;
        SetEvent(hBossSpecialEvent);

        int oldTurn = currentTurn;

        LeaveCriticalSection(&cs);

        Sleep(30);

        EnterCriticalSection(&cs);

        if (bayum.health <= 0 || alivePlayers <= 0) {
            ResetEvent(hBossSpecialEvent);
            LeaveCriticalSection(&cs);
            return 0;
        }

        int baseDamage = (alivePlayers > 1)
            ? static_cast<int>(bayum.specialDamage * (1 - 0.05 * (alivePlayers - 1)))
            : bayum.specialDamage;

        for (int i = 0; i < countPlayers; i++) {
            if (players[i].isDead) continue;

            if (rnd(1, 100) <= players[i].dodgeChance) {
                setColor(10);
                cout << players[i].name << " увернулся\n";
            }
            else {
                int dmg = baseDamage * (100 - players[i].defense) / 100;
                players[i].health -= dmg;
                if (players[i].health < 0) players[i].health = 0;

                setColor(12);
                cout << players[i].name
                    << " получил " << dmg
                    << " | HP: " << players[i].health << endl;

                markPlayerDeadLocked(i);
            }
        }

        int nextTurn = -1;
        if (alivePlayers > 0 && oldTurn != currentTurn) {
            nextTurn = currentTurn;
        }

        ResetEvent(hBossSpecialEvent);
        LeaveCriticalSection(&cs);

        if (nextTurn != -1) {
            SetEvent(hPlayerTurnEvents[nextTurn]);
        }
    }
}

DWORD WINAPI PlayerThread(LPVOID param) {
    int id = (int)(INT_PTR)param;
    HANDLE waits[2] = { hPlayerTurnEvents[id], hBossSpecialEvent };

    while (true) {
        DWORD waitResult = WaitForMultipleObjects(2, waits, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0 + 1) {
            while (WaitForSingleObject(hBossSpecialEvent, 0) == WAIT_OBJECT_0) {
                Sleep(1);
            }
            continue;
        }

        while (true) {
            EnterCriticalSection(&cs);

            if (players[id].isDead || bayum.health <= 0 || alivePlayers <= 0) {
                LeaveCriticalSection(&cs);
                return 0;
            }

            if (id != currentTurn) {
                LeaveCriticalSection(&cs);
                break;
            }

            DWORD now = GetTickCount();
            if (now < players[id].nextAttackTime) {
                DWORD waitMs = players[id].nextAttackTime - now;
                LeaveCriticalSection(&cs);

                DWORD cooldownWait = WaitForSingleObject(hBossSpecialEvent, waitMs);
                if (cooldownWait == WAIT_OBJECT_0) {
                    while (WaitForSingleObject(hBossSpecialEvent, 0) == WAIT_OBJECT_0) {
                        Sleep(1);
                    }
                }
                continue;
            }

            bool useSpecial = (now - players[id].lastSpecialTime >= players[id].specialCooldown * 1000);
            int dmg;

            if (useSpecial) {
                dmg = players[id].specialDamage * (100 - bayum.resist) / 100;
                players[id].lastSpecialTime = now;

                setColor(11);
                cout << players[id].name << " использует СПЕЦ атаку на " << dmg;
            }
            else {
                dmg = players[id].damage * (100 - bayum.resist) / 100;

                setColor(14);
                cout << players[id].name << " атакует на " << dmg;
            }

            bayum.health -= dmg;
            if (bayum.health < 0) bayum.health = 0;

            players[id].dealtDamage += dmg;
            players[id].nextAttackTime = now + players[id].attackCooldown * 1000;

            cout << " | HP босса: " << bayum.health << endl;

            int nextTurn = -1;
            if (bayum.health > 0 && alivePlayers > 0) {
                nextTurn = findNextAlivePlayerLocked(id);
                if (nextTurn != -1) {
                    currentTurn = nextTurn;
                }
            }

            LeaveCriticalSection(&cs);

            if (nextTurn != -1) {
                SetEvent(hPlayerTurnEvents[nextTurn]);
            }

            break;
        }
    }
}

int main() {
    setlocale(LC_ALL, "rus");
    srand(time(0));

    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    setColor(7);

    do {
        cout << "Введите количество игроков (1-10): ";
        cin >> countPlayers;
    } while (countPlayers < 1 || countPlayers > 10);

    alivePlayers = countPlayers;

    for (int i = 0; i < countPlayers; i++) {
        setColor(7);
        cout << "Введите имя игрока " << i + 1 << ": ";
        cin >> players[i].name;
    }

    DWORD startTime = GetTickCount();
    for (int i = 0; i < countPlayers; i++) {
        players[i].lastSpecialTime = startTime;
        players[i].nextAttackTime = startTime;
    }

    InitializeCriticalSection(&cs);

    for (int i = 0; i < countPlayers; i++) {
        hPlayerTurnEvents[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
    }
    hBossSpecialEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    CreateThread(NULL, 0, BossThread, NULL, 0, NULL);
    CreateThread(NULL, 0, BossSpecial, NULL, 0, NULL);

    for (int i = 0; i < countPlayers; i++) {
        CreateThread(NULL, 0, PlayerThread, (LPVOID)(INT_PTR)i, 0, NULL);
    }

    SetEvent(hPlayerTurnEvents[currentTurn]);

    while (true) {
        EnterCriticalSection(&cs);
        bool bossDead = bayum.health <= 0;
        bool teamDead = alivePlayers <= 0;
        LeaveCriticalSection(&cs);

        if (bossDead || teamDead) {
            setColor(7);

            if (bossDead)
                cout << "\nБосс побежден!\n";
            else
                cout << "\nВсе игроки погибли\n";

            sortPlayers();

            cout << "\nТОП-3 игроков:\n";
            for (int i = 0; i < countPlayers && i < 3; i++) {
                cout << i + 1 << ". " << players[i].name
                    << " Урон: " << players[i].dealtDamage << endl;
            }

            ExitProcess(0);
        }

        Sleep(100);
    }
}
