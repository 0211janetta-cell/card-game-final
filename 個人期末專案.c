#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

/* ====== 常數設定 ====== */
#define NUM_CARDS 52
#define HAND_SIZE 7

/* ====== 顏色 ====== */
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_MAG     "\033[35m"
#define C_CYAN    "\033[36m"

/* 花色：0 = ♠, 1 = ♥, 2 = ♣, 3 = ♦ */
typedef struct {
    int suit; // 0~3
    int rank; // 1~13 (1 = A, 11 = J, 12 = Q, 13 = K)
} Card;

/* 遊戲狀態：之後可以慢慢加東西進來 */
typedef struct {
    Card *deck;      // 整副牌（動態配置）
    int deckIndex;   // 下一張要抽的位置（0~51）
    Card *hand;      // 玩家手牌（動態配置，固定 7 張）
    int level;       // 目前關卡
    double score;    // 目前分數
    double target;   // 這一關需要達到的目標分數

    int gold;        // 目前金幣（Gold）

    double singleScore; // 本關 Single 的分數
    double pairScore;   // 本關 Pair 的分數
    double pairBonus;   // 來自 Magic Card 的 Pair 額外加分（永久累積）

    int hasSuitChange;

    int handsUsed;   // 本關已經出了幾手牌

    int hasRedraw; 
    int redrawUsedThisLevel;

    int hasDrawBoost;   // 是否目前手上有一張 Draw Boost（尚未發動）
    int drawBoostUsed;  // 這一輪遊戲中是否已經發動過 Draw Boost（跨關不重置）

    int rankMultiplier[14]; // 1~13：這個點數是否被 Card Multiplier 強化（1 表示有）
    int comboCount;         // 目前的連擊數（只計非 Single）
} GameState;

/* 牌型相關 */
typedef enum {
    HAND_INVALID = 0,     // 不合法 / 不支援的出牌
    HAND_SINGLE,          // 單張
    HAND_PAIR,            // 一對
    HAND_STRAIGHT,        // 順子
    HAND_FLUSH,           // 同花
    HAND_FULL_HOUSE,      // 葫蘆（3+2）
    HAND_FOUR_KIND,       // 四條
    HAND_STRAIGHT_FLUSH,  // 同花順
} HandType;

/* 音效播放（避免疊音版） */
void playSound(const char *path) {
    system("killall afplay >/dev/null 2>&1");   // 先停掉上一個正在播的 afplay

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "afplay \"%s\" >/dev/null 2>&1 &", path);
    system(cmd);
}

/* 初始化遊戲：配置記憶體、設定初始狀態（但不洗牌、不發牌） */
void initGame(GameState *game);

/* 根據 level 設定這一關的目標分數與規則（之後會再補） */
void setupLevel(GameState *game, int level);

void initDeck(Card *deck);

/* 洗牌：Fisher-Yates 洗牌法 */
void shuffleDeck(Card *deck);

/* 發初始牌（7 張） */
void dealInitialHand(GameState *game);

/* 顯示一張牌 */
void printCard(const Card *c);

/* 顯示整個手牌 */
void printHand(const Card *hand);

/* 玩家出牌 → 暫時只做「選幾張牌」 + 回傳那幾張（之後會加牌型判斷） */
int playerPlayHand(GameState *game, Card *played, int *playedCount);

void sortByRank(Card *cards, int n);
int isFlush(Card *cards, int n);
int isStraight(Card *cards, int n);
HandType classifyHand(Card *played, int playedCount);
double handTypeBaseScore(HandType type);
const char *handTypeName(HandType type);
const char *suitSymbol(int s);

/* 根據出的牌來計分（之後實作牌型判斷邏輯） */
double evaluateHand(Card *played, int playedCount, const GameState *game, int *outHasBoost);

/* 移除手牌中剛剛打出的牌，並從牌堆補到 7 張 */
void updateHandAfterPlay(GameState *game, Card *played, int playedCount);

/* 主遊戲迴圈：這一關從開始玩到結束（過關或失敗） */
int playLevel(GameState *game);

void chooseMagicCard(GameState *game);

void applySuitChangeMagic(GameState *game);

void tryUseDrawBoost(GameState *game);

void shopSystem(GameState *game);

void waitEnter(void);

/* 釋放動態記憶體 */
void freeGame(GameState *game);


/* ====== main 函式 ====== */
int main() {
    srand((unsigned int)time(NULL)); // rand() 初始化

    GameState game;
    initGame(&game);

    while (1) {       // 一輪遊戲（1~5 關），結束後可選擇重玩
        int clearedAll = 1;  // 假設一開始會通關，若中途失敗再改成 0

        // 從第 1 關一路玩到第 5 關
        for (int lv = 1; lv <= 5; lv++) {
            // 設定這一關的目標分數 + Single/Pair 規則 + handsUsed 歸零
            setupLevel(&game, lv);

            // 每一關開始前，把分數歸零
            game.score = 0.0;

            // 重新建立牌堆、洗牌、發新的起手牌
            initDeck(game.deck);
            shuffleDeck(game.deck);
            game.deckIndex = 0;
            dealInitialHand(&game);

            if (game.hasSuitChange) {
                applySuitChangeMagic(&game);
            }

            // 開始這一關
            int ok = playLevel(&game);
            if (!ok) {
                // 這一關失敗，結束本輪遊戲
                printf("遊戲在第 %d 關結束。\n", lv);
                clearedAll = 0;   // 沒有通過所有關卡
                break;            // 跳出 for 迴圈，去問要不要再玩一次
            }

            // 如果 ok == 1，代表這一關過關
            if (lv < 5) {
                printf("\n=== 你已通過第 %d 關 ===\n", lv);

                // 1) 免費二選一
                chooseMagicCard(&game);

                // 2) 商店（花 Gold 買）
                shopSystem(&game);

                printf("準備進入第 %d 關。\n\n", lv + 1);
            }
        }

        if (clearedAll) {
            printf("\n恭喜你通過所有關卡！\n");
        }

        // 問玩家要不要再玩一次
        int replay;
        printf("\n要再玩一次嗎？(1 = 再玩一次, 0 = 離開)：");
        if (scanf("%d", &replay) != 1 || replay == 0) {
            printf("謝謝遊玩！\n");
            break;  // 跳出 while(1)，準備結束程式
        }

        // ==== 重設 GameState，準備新的一輪 ====
        game.level        = 1;
        game.score        = 0.0;
        game.target       = 0.0;
        game.gold         = 0;      // 重新開始一輪時 Gold 歸零
        game.hasSuitChange = 0;
        game.handsUsed    = 0;
        game.pairBonus    = 0.0;     // Magic 加成重置
        game.deckIndex    = 0;

        // 重抽功能相關
        game.hasRedraw = 0;
        game.redrawUsedThisLevel = 0;
 
        // Draw Boost 系統
        game.hasDrawBoost  = 0;
        game.drawBoostUsed = 0;

        game.comboCount    = 0;      // 新的一輪，連擊也歸零

        // deck / hand 不用重新 malloc，因為 initGame 已經配好記憶體

        // Card Multiplier：新的一輪遊戲要重置
        for (int r = 1; r <= 13; r++) {
            game.rankMultiplier[r] = 0;
        }
    }

    freeGame(&game);  // 只在最後一次離開時釋放記憶體
    return 0;
}

/* ====== 函式實作區 ====== */
void initGame(GameState *game) {
    // 配置記憶體給 deck 與 hand
    game->deck = malloc(NUM_CARDS * sizeof(Card));
    game->hand = malloc(HAND_SIZE * sizeof(Card));

    if (game->deck == NULL || game->hand == NULL) {
        printf("記憶體配置失敗！\n");
        exit(1);
    }

    game->deckIndex = 0;
    game->score = 0.0;
    game->level = 1;
    game->target = 0.0;
    game->gold = 0;      // 開新遊戲金幣從 0 開始
    game->hasSuitChange = 0;
    game->handsUsed = 0;
    game->pairBonus = 0.0;
    game->hasRedraw = 0;
    game->redrawUsedThisLevel = 0;
    game->hasDrawBoost = 0;
    game->drawBoostUsed = 0;
    game->comboCount = 0;

    // Card Multiplier：一開始全部都沒有被強化
    for (int r = 1; r <= 13; r++) {
        game->rankMultiplier[r] = 0;
    }
}

void setupLevel(GameState *game, int level) {
    game->level = level;

    double baseSingle, basePair;

    if (level == 1) {
        game->target = 55.0;
        baseSingle   = 1.0;
        basePair     = 2.0;
    }
    else if (level == 2) {
        game->target = 60.0;
        baseSingle   = 0.5;
        basePair     = 4.0;
    }
    else if (level == 3) {
        game->target = 65.0;
        baseSingle   = 0.0;
        basePair     = 4.0;
    }
    else if (level == 4) {
        game->target = 70.0;
        baseSingle   = 0.0;
        basePair     = 4.5;
    }
    else if (level == 5) {
        game->target = 75.0;
        baseSingle   = 0.0;
        basePair     = 5.0;
    }
    else {
        game->target = 55.0;
        baseSingle   = 1.0;
        basePair     = 2.0;
    }

    // 套用 Magic Card 加成（必須放在後面）
    game->singleScore = baseSingle;                   // Single 沒有魔法加成
    game->pairScore   = basePair + game->pairBonus;   // Pair = Level 基礎分 + 魔法加成
    game->handsUsed = 0;  // 重設本關的出牌次數
    game->comboCount  = 0;  // 每一關開始時，連擊歸零
    game->redrawUsedThisLevel = 0;
    game->drawBoostUsed = 0;
}

void initDeck(Card *deck) {
    int index = 0;
    for (int suit = 0; suit < 4; suit++) {        // 4 種花色
        for (int rank = 1; rank <= 13; rank++) {  // 1~13
            deck[index].suit = suit;
            deck[index].rank = rank;
            index++;
        }
    }
}

void shuffleDeck(Card *deck) {
    for (int i = NUM_CARDS - 1; i > 0; i--) {
        int j = rand() % (i + 1); // 0~i
        Card temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }
}

void dealInitialHand(GameState *game) {
    for (int i = 0; i < HAND_SIZE; i++) {
        game->hand[i] = game->deck[game->deckIndex];
        game->deckIndex++;
    }
}

void waitEnter(void){
    int ch;

    // 清掉前一次 scanf 留下的殘留（通常是 '\n'）
    while ((ch = getchar()) != '\n' && ch != EOF) {}

    printf("\n%s按 Enter 繼續...%s", C_YELLOW, C_RESET);
    fflush(stdout);

    // 等玩家按 Enter（讀到 '\n' 才結束）
    while ((ch = getchar()) != '\n' && ch != EOF) {}

    printf("\n");
}

void printCard(const Card *c) {
    char *suitChar;
    const char *color;
    switch (c->suit) {
        case 0: suitChar = "♠"; color = C_CYAN; break;
        case 1: suitChar = "♥"; color = C_RED;  break;
        case 2: suitChar = "♣"; color = C_CYAN; break;
        case 3: suitChar = "♦"; color = C_RED;  break;
        default: suitChar = "?"; color = "";    break;
    }

    const char *rankStr;
    static char buf[3];
    switch (c->rank) {
        case 1:  rankStr = "A"; break;
        case 11: rankStr = "J"; break;
        case 12: rankStr = "Q"; break;
        case 13: rankStr = "K"; break;
        default:
            snprintf(buf, sizeof(buf), "%d", c->rank);
            rankStr = buf;
    }

    printf("%s%s%s%s", color, suitChar, rankStr, C_RESET);
}

/* 取得花色顏色（紅/青） */
const char *suitColor(int suit){
    if (suit == 1 || suit == 3) return C_RED;   // ♥ ♦
    return C_CYAN;                               // ♠ ♣
}

/* 取得點數字串（A,2..10,J,Q,K） */
const char *rankText(int rank){
    static char buf[3];
    switch(rank){
        case 1:  return "A";
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        default:
            snprintf(buf, sizeof(buf), "%d", rank);
            return buf;
    }
}

/* 印 n 個空白 */
static void printSpaces(int n){
    for(int i=0;i<n;i++) putchar(' ');
}

/*
 * 印單張卡牌的某一行（line=0~4）
 * selected=1 → 框線與index用 黃+粗體
 */
void printCardBoxLineSelected(const Card *c, int idx, int line, int selected){
    const char *s = suitSymbol(c->suit);
    const char *r = rankText(c->rank);

    const char *faceCol = suitColor(c->suit);               // 牌面紅/青
    const char *boxCol  = selected ? C_YELLOW : C_RESET;    // 框線顏色
    const char *boxBold = selected ? C_BOLD   : "";         // 框線粗體

    // 牌面可視長度：suit(1) + rank(1或2)
    int rankLen = (c->rank == 10) ? 2 : 1;
    int visLen = 1 + rankLen;
    int innerW = 5; // 框內寬度
    int leftPad  = (innerW - visLen) / 2;
    int rightPad = innerW - visLen - leftPad;

    if (line == 0) {
        printf("%s%s┌─────┐%s", boxCol, boxBold, C_RESET);
    } else if (line == 1) {
        // index行：被選中就黃粗體
        printf("%s%s│ [%d] │%s", boxCol, boxBold, idx, C_RESET);
    } else if (line == 2) {
        // 牌面：框線用box色，牌面用紅/青
        printf("%s%s│%s", boxCol, boxBold, C_RESET);
        printSpaces(leftPad);
        printf("%s%s%s%s", faceCol, s, r, C_RESET);
        printSpaces(rightPad);
        printf("%s%s│%s", boxCol, boxBold, C_RESET);
    } else if (line == 3) {
        printf("%s%s│     │%s", boxCol, boxBold, C_RESET);
    } else { // line == 4
        printf("%s%s└─────┘%s", boxCol, boxBold, C_RESET);
    }
}

/*
 * 印整副手牌（橫向框框）
 * selected[i]=1 → 第 i 張高亮（黃+粗體框線）
 */
void printHandBoxedSelected(const Card *hand, const int selected[HAND_SIZE]){
    printf("你的手牌：\n");
    for (int line = 0; line < 5; line++){
        for (int i = 0; i < HAND_SIZE; i++){
            int sel = selected ? selected[i] : 0;
            printCardBoxLineSelected(&hand[i], i, line, sel);
            printf(" ");
        }
        printf("\n");
    }
}

/* 沒有選取狀態時的簡化版（全都不高亮） */
void printHandBoxed(const Card *hand){
    int dummy[HAND_SIZE] = {0};
    printHandBoxedSelected(hand, dummy);
}

/* 印 3 張候選卡（橫向框框） */
void print3CardsBoxed(const Card cards[3]) {
    for (int line = 0; line < 5; line++) {
        for (int i = 0; i < 3; i++) {
            // 這裡 selected 一律 0，代表不高亮
            printCardBoxLineSelected(&cards[i], i, line, 0);
            printf(" ");
        }
        printf("\n");
    }
}

/* ===== Suit 選擇框框（4個） ===== */
/* 印單個 suit 選擇框框的某一行（line=0~4）
 * selected=1 → 黃框粗體
 */
void printSuitBoxLineSelected(int suit, int idx, int line, int selected){
    const char *sym = suitSymbol(suit);      // 方案A：回傳純符號 "♠"
    const char *faceCol = suitColor(suit);   // 紅/青
    const char *boxCol  = selected ? C_YELLOW : C_RESET;
    const char *boxBold = selected ? C_BOLD   : "";

    if (line == 0) {
        printf("%s%s┌─────┐%s", boxCol, boxBold, C_RESET);
    } else if (line == 1) {
        printf("%s%s│ [%d] │%s", boxCol, boxBold, idx, C_RESET);
    } else if (line == 2) {
        // 中間放花色符號
        printf("%s%s│%s", boxCol, boxBold, C_RESET);
        printf("  "); // 左邊兩格
        printf("%s%s%s", faceCol, sym, C_RESET);
        printf("  "); // 右邊兩格
        printf("%s%s│%s", boxCol, boxBold, C_RESET);
    } else if (line == 3) {
        printf("%s%s│     │%s", boxCol, boxBold, C_RESET);
    } else { // line == 4
        printf("%s%s└─────┘%s", boxCol, boxBold, C_RESET);
    }
}

/* 印 4 個 suit 選項（橫向框框）
 * selectedSuit = -1 表示都不亮；0~3 表示那個亮黃框
 */
void printSuitOptionsBoxed(int selectedSuit){
    for(int line = 0; line < 5; line++){
        for(int s = 0; s < 4; s++){
            int sel = (s == selectedSuit);
            printSuitBoxLineSelected(s, s, line, sel);
            printf(" ");
        }
        printf("\n");
    }
}

void printHand(const Card *hand) {
    printf("你的手牌：\n");
    for (int i = 0; i < HAND_SIZE; i++) {
        printf("[%d] ", i);  // 顯示 index，方便玩家選牌
        printCard(&hand[i]);
        printf("\n");
    }
}

/*
 * 玩家出牌
 * 1. 顯示手牌
 * 2. 問玩家要出幾張牌（例如輸入 1, 2, 5...）
 * 3. 一個一個輸入 index，把那幾張牌複製到 played[]
 * 4. 回傳 playedCount
 *
 * 之後我們會在這裡加上：
 * - 判斷這手牌是不是合法牌型
 * - 如果不合法，就請玩家重選
 */
int playerPlayHand(GameState *game, Card *played, int *playedCount) {
    int selected[HAND_SIZE] = {0};

    // 只印一次手牌（不要每選一張就重印）
    printHandBoxedSelected(game->hand, selected);

    int count;
    printf("你想出幾張牌？(可出 1 / 2 / 5，輸入 0 結束回合): ");
    if (scanf("%d", &count) != 1 || count < 0 || count > 5) {
        return 0;
    }

    if (count == 0) {
        return 0;
    }

    int used[HAND_SIZE] = {0};

    for (int i = 0; i < count; i++) {
        int idx;
        printf("請輸入第 %d 張要出的牌 index：", i + 1);
        if (scanf("%d", &idx) != 1) return 0;

        if (idx < 0 || idx >= HAND_SIZE) {
            printf("%s輸入超出範圍，回合作廢。%s\n", C_RED, C_RESET);
            return 0;
        }
        if (used[idx]) {
            printf("%s這張牌你已經選過了，回合作廢。%s\n", C_RED, C_RESET);
            return 0;
        }

        used[idx] = 1;
        selected[idx] = 1;
        played[i] = game->hand[idx];

        // 不重印整副牌，只給 feedback
        printf("已選：[%d] ", idx);
        printCard(&game->hand[idx]);
        printf("\n");

        printf("目前已選 index：");
        for (int k = 0; k < HAND_SIZE; k++) {
            if (selected[k]) printf("[%d] ", k);
        }
        printf("\n");
    }

    // 最後一次再印高亮確認（印一次就好）
    printf("\n%s你本回合選到的牌（高亮確認）：%s\n", C_BOLD, C_RESET);
    printHandBoxedSelected(game->hand, selected);

    // 牌型合法性檢查
    HandType type = classifyHand(played, count);
    if (type == HAND_INVALID) {
        printf("%s這不是合法的牌型，這一回合作廢。%s\n", C_RED, C_RESET);
        return 0;
    }

    *playedCount = count;
    return 1;
}

/* 用 rank 對牌做由小到大的排序（非常單純的 bubble sort） */
void sortByRank(Card *cards, int n) {
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            if (cards[j].rank > cards[j + 1].rank) {
                Card temp = cards[j];
                cards[j] = cards[j + 1];
                cards[j + 1] = temp;
            }
        }
    }
}

/* 檢查是不是同花（花色全部一樣） */
int isFlush(Card *cards, int n) {
    for (int i = 1; i < n; i++) {
        if (cards[i].suit != cards[0].suit) {
            return 0;   // 有一張花色不同 → 不是同花
        }
    }
    return 1;
}

/* 檢查是不是順子（點數連續） */
int isStraight(Card *cards, int n) {
    for (int i = 1; i < n; i++) {
        if (cards[i].rank == cards[i - 1].rank) {
            return 0;   // 有重複點數就一定不是順子
        }
        if (cards[i].rank != cards[i - 1].rank + 1) {
            return 0;   // 不是「上一張+1」就不是連號
        }
    }
    return 1;
}

HandType classifyHand(Card *played, int playedCount) {
    if (playedCount <= 0) return HAND_INVALID;

    if (playedCount == 1) return HAND_SINGLE;

    // 只支援最多 5 張組成牌型
    if (playedCount > 5) return HAND_INVALID;

    // 建一份 copy 來操作（避免改到原本的 played[]）
    Card tmp[5];
    for (int i = 0; i < playedCount; i++) {
        tmp[i] = played[i];
    }

    // 先依照 rank 排序，方便判斷順子與計算
    sortByRank(tmp, playedCount);

    // 統計每個點數出現幾次（rank 1~13）
    int rankCount[14] = {0};  // 全部先設成 0

    for (int i = 0; i < playedCount; i++) {
        int r = tmp[i].rank;
        if (r >= 1 && r <= 13) {
            rankCount[r]++;
        }
    }

    int pairs = 0;   // 有幾組「二張」
    int three = 0;   // 有幾組「三張」
    int four  = 0;   // 有幾組「四張」

    for (int r = 1; r <= 13; r++) {
        if (rankCount[r] == 2) pairs++;
        else if (rankCount[r] == 3) three++;
        else if (rankCount[r] == 4) four++;
    }

    int flush = isFlush(tmp, playedCount);
    int straight = isStraight(tmp, playedCount);

    /* 根據張數來判斷 */
    if (playedCount == 2) {
        if (pairs == 1) return HAND_PAIR;
        return HAND_INVALID;
    }

    if (playedCount == 3) {
        return HAND_INVALID;
    }

     if (playedCount == 4) {
        return HAND_INVALID;
    }

    if (playedCount == 5) {
        if (straight && flush) return HAND_STRAIGHT_FLUSH;
        if (four == 1)         return HAND_FOUR_KIND;
        if (three == 1 && pairs == 1) return HAND_FULL_HOUSE;
        if (flush)             return HAND_FLUSH;
        if (straight)          return HAND_STRAIGHT;
        return HAND_INVALID;
    }
    return HAND_INVALID; // C語言規定一定要有回傳值
}

double handTypeBaseScore(HandType type) {
    switch (type) {
        case HAND_STRAIGHT:        return 5.0;
        case HAND_FLUSH:           return 6.0;
        case HAND_FULL_HOUSE:      return 8.0;
        case HAND_FOUR_KIND:       return 10.0;
        case HAND_STRAIGHT_FLUSH:  return 12.0;
        default:                   return 0.0;
    }
}

const char *handTypeName(HandType type) {
    switch (type) {
        case HAND_SINGLE:          return "Single";
        case HAND_PAIR:            return "Pair";
        case HAND_STRAIGHT:        return "Straight";
        case HAND_FLUSH:           return "Flush";
        case HAND_FULL_HOUSE:      return "Full House";
        case HAND_FOUR_KIND:       return "Four of a Kind";
        case HAND_STRAIGHT_FLUSH:  return "Straight Flush";
        case HAND_INVALID:
        default:                   return "Invalid";
    }
}

const char *suitSymbol(int s) {
    switch (s) {
        case 0: return "♠";
        case 1: return "♥";
        case 2: return "♣";
        case 3: return "♦";
        default: return "?";
    }
}

double evaluateHand(Card *played, int playedCount, const GameState *game, int *outHasBoost) {
    if (outHasBoost) *outHasBoost = 0;

    if (playedCount <= 0) return 0.0;
    if (playedCount > 5)  return 0.0;

    HandType type = classifyHand(played, playedCount);
    if (type == HAND_INVALID) return 0.0;

    double finalScore = 0.0;

    if (type == HAND_SINGLE) {
        finalScore = game->singleScore;
    } else if (type == HAND_PAIR) {
        finalScore = game->pairScore;
    } else {
        finalScore = handTypeBaseScore(type);
    }

    // Card Multiplier：檢查是否有被強化的 rank
    int hasBoostRank = 0;
    for (int i = 0; i < playedCount; i++) {
        int r = played[i].rank;
        if (r >= 1 && r <= 13 && game->rankMultiplier[r]) {
            hasBoostRank = 1;
            break;
        }
    }

    if (hasBoostRank) {
        finalScore *= 1.5;
        if (outHasBoost) *outHasBoost = 1;
    }

    return finalScore; // 回傳：尚未套用 Combo 的分數
}

void applySuitChangeMagic(GameState *game) {
    printf("\n=== Suit Change Magic Card ===\n");
    printf("你可以把手牌中「一張牌」的花色改成你指定的花色。\n");

    /* --- A) 選要改的那張牌（用手牌框框 + 黃框） --- */
    int selected[HAND_SIZE] = {0};
    printf("目前你的起手牌是：\n");
    printHandBoxedSelected(game->hand, selected);

    int idx;
    printf("請輸入要改花色的牌的 index（0 ~ %d）：", HAND_SIZE - 1);
    if (scanf("%d", &idx) != 1 || idx < 0 || idx >= HAND_SIZE) {
        printf("輸入錯誤，Suit Change 魔法作廢。\n");
        game->hasSuitChange = 0;
        return;
    }

    // 亮黃框顯示你選到的那張
    for(int i=0;i<HAND_SIZE;i++) selected[i]=0;
    selected[idx] = 1;
    printf("\n你選擇要改的牌是：\n");
    printHandBoxedSelected(game->hand, selected);

    /* --- B) 選新花色（4 個花色框框 + 黃框） --- */
    printf("\n請選擇新的花色（輸入 0~3）：\n");
    printSuitOptionsBoxed(-1);

    int newSuit;
    printf("輸入花色編號：");
    if (scanf("%d", &newSuit) != 1 || newSuit < 0 || newSuit > 3) {
        printf("輸入錯誤，Suit Change 魔法作廢。\n");
        game->hasSuitChange = 0;
        return;
    }

    // 亮黃框顯示你選到的花色
    printf("\n你選擇的新花色是：\n");
    printSuitOptionsBoxed(newSuit);

    int oldSuit = game->hand[idx].suit;
    game->hand[idx].suit = newSuit;

    printf("\n已將第 %d 張牌的花色從 ", idx);
    printf("%s%s%s", suitColor(oldSuit), suitSymbol(oldSuit), C_RESET);
    printf(" 改成 ");
    printf("%s%s%s。\n", suitColor(newSuit), suitSymbol(newSuit), C_RESET);

    printf("修改後的手牌：\n");
    printHandBoxed(game->hand);

    game->hasSuitChange = 0;  // 用掉
}

void tryUseDrawBoost(GameState *game) {
    // 檢查是否真的有這張牌 & 還沒用過
    if (!game->hasDrawBoost || game->drawBoostUsed) {
        printf("目前無法使用 Draw Boost。\n");
        return;
    }

    // 檢查牌堆是否還夠抽 3 張
    if (game->deckIndex + 3 > NUM_CARDS) {
        printf("牌堆剩餘牌數不足，無法使用 Draw Boost。\n");
        return;
    }

    Card candidates[3];
    for (int i = 0; i < 3; i++) {
        candidates[i] = game->deck[game->deckIndex];
        game->deckIndex++;
    }

    printf("\n=== Draw Boost 發動！===\n");
    printf("從牌堆抽出 3 張牌：\n");
    print3CardsBoxed(candidates);

    int pick;
    printf("請選擇你要留下的牌（輸入 0~2）：");
    if (scanf("%d", &pick) != 1 || pick < 0 || pick >= 3) {
        printf("輸入錯誤，Draw Boost 取消。\n");
        return;
    }

    printf("\n你目前的手牌為：\n");
    printHandBoxed(game->hand);

    int replaceIndex;
    printf("請選擇要被替換掉的手牌 index（0 ~ %d）：", HAND_SIZE - 1);
    if (scanf("%d", &replaceIndex) != 1 ||
        replaceIndex < 0 || replaceIndex >= HAND_SIZE) {
        printf("輸入錯誤，Draw Boost 取消。\n");
        return;
    }

    printf("你將手牌第 %d 張換成 ", replaceIndex);
    printCard(&candidates[pick]);
    printf("。\n");

    game->hand[replaceIndex] = candidates[pick];

    // 其餘 2 張直接丟棄（不放回牌堆）

    game->hasDrawBoost  = 0;  // 這張 Magic Card 用掉了
    game->drawBoostUsed = 1;  // 這一輪遊戲已經發動過 Draw Boost
}

void chooseMagicCard(GameState *game) {
    printf("\n=== ChooseMagicCard（免費二選一）===\n");

    // 你可以固定 +1 或隨機 1~3（我先保留你原本的隨機）
    int bonus = (rand() % 3) + 1;

    printf("請從以下兩張 Basic Magic Card 選一張（免費）：\n");
    printf(" [1] Hand Score Upgrade\n");
    printf("     效果：之後所有關卡 Pair 額外 +%d 分（永久累積）\n\n", bonus);

    printf(" [2] Suit Change\n");
    printf("     效果：下一關開始時，可把起手牌其中一張改花色一次\n\n");

    int choice;
    while (1) {
        printf("請輸入 1 或 2：");
        if (scanf("%d", &choice) != 1) {
            printf("輸入錯誤，請重試。\n");
            continue;
        }
        if (choice == 1 || choice == 2) break;
        printf("只能選 1 或 2。\n");
    }

    if (choice == 1) {
        game->pairBonus += bonus;
        printf("\n你選擇了 Hand Score Upgrade！\n");
        printf("目前累積的 Pair 額外加分總共：+%.1f 分。\n", game->pairBonus);
    } else {
        game->hasSuitChange = 1;
        printf("\n你選擇了 Suit Change！\n");
        printf("將在【下一關開始時】對起手牌使用一次。\n");
    }
}

void shopSystem(GameState *game) {
    printf("\n=== Shop（花 Gold 購買）===\n");

    const int COST_DRAW  = 25;
    const int COST_MULTI = 30;
    const int COST_REDRAW = 20;

    while (1) {
        printf("\n你目前 %sGold：%d%s\n", C_YELLOW, game->gold, C_RESET);
        printf("你可以選擇購買：\n");
        printf(" [1] Draw Boost（%d Gold）\n", COST_DRAW);
        printf("     效果：下一次回合可抽 3 選 1，替換手牌一次（每關最多一次、不能囤多張）\n\n");

        printf(" [2] Card Multiplier（%d Gold）\n", COST_MULTI);
        printf("     效果：隨機強化一個 rank，之後出牌含該 rank → 該手分數 x1.5（永久）\n\n");

        printf(" [3] Redraw（%d Gold）\n", COST_REDRAW);
        printf("     效果：本關可重抽整手牌一次（每關最多一次、不能囤多張）\n\n");

        printf(" [0] 離開商店\n\n");

        int choice;
        printf("請輸入 0 / 1 / 2 / 3：");
        if (scanf("%d", &choice) != 1) {
            printf("輸入錯誤，請重試。\n");
            continue;
        }

        if (choice == 0) {
            printf("離開商店。\n");
            return;
        }

        if (choice == 1) {
            if (game->hasDrawBoost) {
                printf("\n⚠ 你已經持有尚未使用的 Draw Boost，不能再買一張。\n");
                continue;
            }
            if (game->gold < COST_DRAW) {
                printf("%s%s\nGold 不足！需要 %d，但你只有 %d。%s\n", C_RED, C_BOLD, COST_DRAW, game->gold, C_RESET);
                continue;
            }
            game->gold -= COST_DRAW;
            game->hasDrawBoost = 1;
            printf("\n購買成功：Draw Boost！剩餘 Gold：%d\n", game->gold);
            return;
        }

        if (choice == 2) {
            if (game->gold < COST_MULTI) {
                printf("%s%s\nGold 不足！需要 %d，但你只有 %d。%s\n", C_RED, C_BOLD, COST_MULTI, game->gold, C_RESET);
                continue;
            }

            int available[13], cnt = 0;
            for (int r = 1; r <= 13; r++) {
                if (game->rankMultiplier[r] == 0) {
                    available[cnt++] = r;
                }
            }
            if (cnt == 0) {
                printf("\n⚠ 1~13 全都已被強化，無法再買 Card Multiplier。\n");
                continue;
            }

            game->gold -= COST_MULTI;

            int chosenRank = available[rand() % cnt];
            game->rankMultiplier[chosenRank] = 1;

            printf("\n購買成功：Card Multiplier！剩餘 Gold：%d\n", game->gold);
            printf("已隨機強化點數：%d（之後出牌含 %d → 該手分數 x1.5）\n", chosenRank, chosenRank);
            return;
        }

        if (choice == 3) {
            if (game->hasRedraw) {
                printf("\n⚠ 你已經持有一張 Redraw，不能再買。\n");
                continue;
            }
            if (game->gold < COST_REDRAW) {
                printf("%s%s\nGold 不足！需要 %d，但你只有 %d。%s\n", C_RED, C_BOLD, COST_REDRAW, game->gold, C_RESET);
                continue;
            }
            game->gold -= COST_REDRAW;
            game->hasRedraw = 1;
            printf("\n購買成功：Redraw！剩餘 Gold：%d\n", game->gold);
            return;
        }

        printf("只能輸入 0 / 1 / 2 / 3。\n");
    }
}

void updateHandAfterPlay(GameState *game, Card *played, int playedCount) {
    // 1. 建立一個暫存的新手牌陣列
    Card newHand[HAND_SIZE];
    int newIndex = 0;

    // 2. 把沒有被出的牌搬進 newHand
    for (int i = 0; i < HAND_SIZE; i++) {
        int isPlayed = 0;

        // 檢查 hand[i] 是否在 played[] 裡
        for (int j = 0; j < playedCount; j++) {
            if (game->hand[i].suit == played[j].suit &&
                game->hand[i].rank == played[j].rank) {
                isPlayed = 1;
                break;
            }
        }

        // 如果 hand[i] 沒有被出 → 放進 newHand
        if (isPlayed==0) {
            newHand[newIndex] = game->hand[i];
            newIndex++;
        }
    }

    // 3. 補牌：補 (7 - newIndex) 張
    int need = HAND_SIZE - newIndex;

    if (game->deckIndex + need > NUM_CARDS) {
        printf("牌堆不夠補新的手牌了！\n");
        return;
    }

    for (int i = 0; i < need; i++) {
        newHand[newIndex] = game->deck[game->deckIndex];
        newIndex++;
        game->deckIndex++;
    }

    // 4. 把 newHand 複製回玩家的手牌
    for (int i = 0; i < HAND_SIZE; i++) {
        game->hand[i] = newHand[i];
    }
}

int playLevel(GameState *game) { 
    printf("=== 開始第 %d 關 ===\n", game->level);
    printf("目標分數：%.1f\n", game->target);
    printf("目前牌堆位置：%d / %d\n\n", game->deckIndex, NUM_CARDS);

    while (1) {
        printf("\n目前分數：%.1f  |  目前 %sGold：%d%s\n",
        game->score, C_YELLOW, game->gold, C_RESET);
        if (game->comboCount > 1) {
            double comboMultiplier = 1.0 + 0.15 * (game->comboCount - 1);
            printf("%s%s當前 Combo：%d 連擊，倍率 x%.2f%s\n", C_MAG, C_BOLD, game->comboCount, comboMultiplier, C_RESET);
        } else if (game->comboCount == 1) {
            printf("當前 Combo：1 連擊（尚未加成）\n");
        } else {
            printf("當前 Combo：無\n");
        }

        if (game->score >= game->target) {
            printf("%s%s恭喜！你已達成目標分數，通過第 %d 關！%s\n", C_GREEN, C_BOLD, game->level, C_RESET);
            printf("你在本關總共出了 %d 手牌。\n", game->handsUsed);
            if (game->level == 5){
                playSound("sounds/遊戲最後一關成功.mp3");
                usleep(1200000);
            }else {
                playSound("sounds/遊戲成功.mp3");
                usleep(900000);
            }
            return 1;   // 用 1 代表「這一關過關」
        }

        if (game->deckIndex >= NUM_CARDS) {
            printf("%s%s牌堆用完了，但分數還沒達到目標，遊戲失敗 QQ%s\n", C_RED, C_BOLD, C_RESET);
            playSound("sounds/遊戲失敗.mp3");
            usleep(1200000);
            return 0;   // 用 0 代表「這一關失敗」
        }

        /* 如果有 Redraw（商店買的），本關可用一次，不扣分 */
        if (game->hasRedraw && !game->redrawUsedThisLevel) {
            int useRedraw;
            printf("\n你擁有一張『Redraw』Magic Card。\n");
            printf("是否要使用？(1 = 使用, 0 = 不使用)：");
            if (scanf("%d", &useRedraw) == 1 && useRedraw == 1) {

                if (game->deckIndex + HAND_SIZE > NUM_CARDS) {
                    printf("牌堆剩餘牌數不足，無法重抽整手牌。\n");
                } else {
                    game->hasRedraw  = 0;   // 用掉
                    game->redrawUsedThisLevel = 1;

                    for (int i = 0; i < HAND_SIZE; i++) {
                        game->hand[i] = game->deck[game->deckIndex];
                        game->deckIndex++;
                    }

                    printf("已重抽整手牌！新的手牌為：\n");
                    printHandBoxed(game->hand);
                    continue; // 用新手牌重新考慮
                }
            }
        }

        /* 如果有 Draw Boost，問玩家這回合要不要用 */
        if (game->hasDrawBoost && !game->drawBoostUsed) {
            int useBoost;
            printf("\n你擁有一張『Draw Boost』Magic Card。\n");
            printf("是否要使用？(1 = 使用, 0 = 不使用)：");
            if (scanf("%d", &useBoost) == 1 && useBoost == 1) {
                tryUseDrawBoost(game);
            }
        }

        // ===== 正常出牌流程 =====
        Card played[HAND_SIZE];
        int playedCount = 0;

        int ok = playerPlayHand(game, played, &playedCount);
        if (!ok || playedCount == 0) {
            printf("你這回合沒有成功出牌。\n");
            playSound("sounds/出牌失敗.mp3");
            usleep(900000);   // 0.8 秒，和你成功音效節奏一致
            game->comboCount = 0;   // 出牌失敗 → 連擊中斷
            continue;
        }
        playSound("sounds/出牌成功.mp3");
        usleep(900000); 
        
        int hasBoost = 0;
        HandType type = classifyHand(played, playedCount);

        // 先算：尚未套用 Combo 的 base gain（但已包含 x1.5 multiplier）
        double baseGain = evaluateHand(played, playedCount, game, &hasBoost);

        double gain = baseGain;      // 之後可能套 combo
        double comboMult = 1.0;
        int brokeCombo = 0;

        // Combo 規則
        if (type == HAND_SINGLE) {
            game->comboCount = 0;
            brokeCombo = 1;
        } else {
            game->comboCount++;
            comboMult = 1.0 + 0.15 * (game->comboCount - 1);
            gain *= comboMult;
        }

        // 更新總分
        game->score += gain;

        // Gold
        int earnGold = (int)gain;
        if (earnGold < 0) earnGold = 0;
        game->gold += earnGold;

        /* ===== 回合結算面板 ===== */
        printf("\n%s───────── 回合結算 ─────────%s\n", C_BOLD, C_RESET);
        printf("牌型：%s\n", handTypeName(type));
        printf("本回合小計（未套 Combo)：%.1f\n", baseGain);

        if (hasBoost) {
            printf("Card Multiplier：%s已觸發(x1.5)%s\n", C_YELLOW, C_RESET);
        }

        if (brokeCombo) {
            printf("Combo：中斷(Single)\n");
        } else {
            printf("Combo：%d 連擊，倍率 x%.2f\n", game->comboCount, comboMult);
        }

        printf("本回合實得分：%s+%.1f%s\n", C_GREEN, gain, C_RESET);
        printf("Gold：%s+%d%s（總額： %s%d%s）\n", C_YELLOW, earnGold, C_RESET, C_YELLOW, game->gold, C_RESET);
        printf("%s────────────────────────────%s\n", C_BOLD, C_RESET);

        waitEnter();

        game->handsUsed++;  // 成功出了一手牌，計數 +1

        updateHandAfterPlay(game, played, playedCount);
    }
}

void freeGame(GameState *game) {
    free(game->deck);
    free(game->hand);
    game->deck = NULL;
    game->hand = NULL;
}