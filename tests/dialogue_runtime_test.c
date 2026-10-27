#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Exercise the actual interpreter, including its private word buffer, while
 * capturing the renderer calls instead of needing an interactive conversation. */
#include "../src/talk.c"

static char output[4096], lastString[4096];
static int styles[4096], length, column, charset;
void __wrap_ULTIMA_16ba_PrintChar(uint ch) {
    assert(length < (int)sizeof(output) - 1);
    output[length] = (char)ch;
    styles[length++] = charset;
    output[length] = 0;
    column = ch == '\n' ? 0 : column + 1;
}
void __wrap_ULTIMA_1850_PrintString(char *text) {
    assert(strlen(text) < sizeof(lastString)); strcpy(lastString, text);
    while (*text) __wrap_ULTIMA_16ba_PrintChar((unsigned char)*text++); }
void __wrap_ULTIMA_1c9e_SelectCharset(int value) { charset = value; }
int __wrap_ULTIMA_1f12_GetCurrentTextX(void) { return column; }
static void reset(void) {
    length = column = charset = 0;
    output[0] = 0;
    D_4aee = D_4aef = D_4af1 = D_4af3 = D_4af4 = D_4af5 = 0;
    D_4af0 = 255;
}
static void text(const char *s) {
    while (*s)
        assert(TALK_0f32_ProcessChar((byte)*s++ | 128) == 0);
}
static int nameInputStep;
static void nameThenBye(void *host, char *answer, int capacity) {
    (void)host; (void)capacity;
    strcpy(answer, nameInputStep++ ? "BYE" : "NAME");
}
static int revealName(void *host) { (void)host; return 1; }
static void enhancedLook(void) {
    const uint8_t conversation[] = {0xc7,0xe1,0xec,0xec,0xf2,0xef,0xf4,0,
        0xe1,0xa0,0xe6,0xe1,0xf4,0xa0,0xe1,0xee,0xe4,0xa0,0xe7,0xf2,0xe5,0xe1,0xf3,0xf9,0xa0,0xed,0xe1,0xee,0xae,0,0x82,0};
    char result[256];
    size_t required = IMPERA_TalkLookText(conversation,sizeof(conversation),0,"Avatar",result,sizeof(result));
    assert(required == strlen("a fat and greasy man.") + 1 && !strcmp(result,"a fat and greasy man."));
    assert(IMPERA_TalkLookText(conversation,sizeof(conversation),1,"Avatar",result,sizeof(result)) == 8);
    assert(!strcmp(result,"Gallrot"));
    uint8_t flags[128]; memcpy(flags,conversation,8);
    memcpy(flags+8,IMPERA_TALK_SKIP_OPENING,4);
    memcpy(flags+12,conversation+8,sizeof(conversation)-8);
    assert(IMPERA_TalkLookText(flags,sizeof(conversation)+4,0,"Avatar",result,sizeof(result)) == required);
    assert(!strcmp(result,"a fat and greasy man."));
    const uint8_t dictionary[] = {0xc7,0,1,0xed,0xe1,0xee,0};
    assert(IMPERA_TalkLookText(dictionary,sizeof(dictionary),0,NULL,result,sizeof(result)));
    assert(!strcmp(result,"the man"));
    const uint8_t action[] = {0xc7,0,0xe1,0xa0,0xed,0xe1,0xee,0x89,0xae,0};
    assert(IMPERA_TalkLookText(action,sizeof(action),0,NULL,result,sizeof(result)));
    assert(!strcmp(result,"a man")); /* No actions run, and trailing action-dependent text is omitted. */
    /* Real NPC lookup must use learned names, not the NPC-knows-Avatar bit. */
    const char *savedResource=D_4aa0[0]; D_4aa0[0]="look-test.tlk";
    FILE *bank=fopen("look-test.tlk","wb"); assert(bank);
    const unsigned char header[]={1,0,1,0,6,0};
    assert(fwrite(header,1,6,bank)==6 && fwrite(conversation,1,sizeof(conversation),bank)==sizeof(conversation));
    assert(!fclose(bank));
    D_539a_currentTextWindow=&D_535e_textWindows[0]; D_539a_currentTextWindow->right=39;
    D_539a_currentTextWindow->bottom=24;
    D_5893_map_id=1; D_bcdc=5; D_659e[5]=0x10;
    D_5f5e[5]._0=1; D_5f5e[5]._c=1; D_5f5e[5]._a=1;
    D_5bda[0] |= 32; TALK_ResetKnowledge(); reset();
    assert(TALK_LookNpc(1) && strstr(output,"You see a fat and greasy man."));
    assert(!learnedNames[0][0] && (D_5bda[0]&32));
    s_conversation=(uint8_t*)conversation; s_conversationSize=sizeof(conversation);
    ImperaTalk session=nativeTalk(); reset();
    D_5bda[0]&=~32; session.greetingRoll=revealName;
    assert(IMPERA_TalkDescription(&session)==0); IMPERA_TalkFlush(&session);
    assert(learnedNames[0][0]&32);
    reset(); assert(TALK_LookNpc(1) && strstr(output,"You see Gallrot."));
    /* Punctuation must reach the word wrapper in the same string as the
     * final word, rather than being printed separately after a full row. */
    assert(!strcmp(lastString,"Gallrot."));
    const uint8_t eb[] = {0xc5,0xe2,0xa2,0x8d,0x8d,0x82,0,0xe1,0xa0,0xe2,0xef,0xf9,0xae,0};
    assert(IMPERA_TalkLookText(eb,sizeof(eb),1,NULL,result,sizeof(result)) == 3 && !strcmp(result,"Eb"));
    learnedNames[0][0]=0;
    s_conversation=(uint8_t*)eb; s_conversationSize=sizeof(eb);
    session=nativeTalk(); session.greetingRoll=revealName; reset();
    assert(IMPERA_TalkDescription(&session)==1); /* Name followed by End conversation. */
    assert(learnedNames[0][0]&32); /* Nonzero script return must not discard a revealed name. */
    /* Eb's real Name control pattern: close quote, two newlines, then
     * return to topics for introduced players; otherwise ask their name. */
    const uint8_t ebScript[]={0xc5,0xe2,0xa2,0x8d,0x8d,0x8c,0xff,0x88,0,
        0xe1,0x28,0xe2,0xef,0xf9,0xae,0,0xc8,0xe9,0,
        0xc2,0xf5,0xf3,0xe2,0xef,0xf9,0,0xc2,0xf9,0xe5,0xa1,0};
    learnedNames[0][0]=0; D_5bda[0]|=32;
    s_conversation=(uint8_t*)ebScript; s_conversationSize=sizeof(ebScript);
    session=nativeTalk(); session.getString=nameThenBye; *session.questionMode=0; nameInputStep=0; reset();
    assert(IMPERA_TalkTopics(&session)==1 && !session.error);
    assert(nameInputStep==2 && (learnedNames[0][0]&32));
    s_conversation=NULL; s_conversationSize=0; D_4aa0[0]=(char*)savedResource;
    memset(&D_5f5e[5],0,sizeof(D_5f5e[5])); D_659e[5]=0; D_5bda[0]&=~32;
    assert(!remove("look-test.tlk"));
    TALK_ResetKnowledge(); D_5893_map_id=25; D_bcdc=40;
    assert(!learnedNames[24][5]); talkLearnName(NULL); assert(learnedNames[24][5]==1);
    FILE *f=tmpfile(); assert(f && TALK_WriteKnowledge(f));
    TALK_ResetKnowledge(); rewind(f); assert(TALK_ReadKnowledge(f,0) && !learnedNames[24][5]);
    rewind(f); assert(TALK_ReadKnowledge(f,1) && learnedNames[24][5]==1);
    assert(!fseek(f,16,SEEK_SET)); int byte=fgetc(f); assert(byte>=0);
    assert(!fseek(f,16,SEEK_SET) && fputc(byte^1,f)!=EOF && !fflush(f));
    rewind(f); assert(!TALK_ReadKnowledge(f,1) && learnedNames[24][5]==1);
    fclose(f); f=tmpfile(); assert(f);
    assert(TALK_ReadKnowledge(f,1) && !learnedNames[24][5]); fclose(f);
    D_5893_map_id=0; D_bcdc=0; TALK_ResetKnowledge();
}
int main(void) {
    enhancedLook();
    for (int pattern = 0; pattern < 16; ++pattern) {
        for (int i = 0; i < 4; ++i)
            D_5d5e[5].time[i] = (byte)(pattern * 19 + i * 73);
        for (int hour = 0; hour < 256; ++hour)
            assert(IMPERA_GameSchedule((const uint8_t *)&D_5d5e[5], (uint8_t)hour) ==
                   NPC_12e0(5, (char)hour));
    }
    reset();
    text("Hi lastword.");
    assert(TALK_0f32_ProcessChar(0x82) == 1);
    assert(!strcmp(output, "Hi lastword.") && D_4af1 == 0);
    reset();
    D_5888 = 10;
    text("complete");
    assert(TALK_0f32_ProcessChar(0x89) == 0);
    assert(!strcmp(output, "complete") && D_5888 == 11);
    text(" again");
    assert(TALK_0f32_ProcessChar(0x82) == 1);
    assert(!strcmp(output, "complete again"));
    reset();
    text("old");
    assert(TALK_0f32_ProcessChar(0x8e) == 0);
    text("new");
    assert(TALK_0f32_ProcessChar(0x82) == 1);
    assert(!strcmp(output, "oldnew"));
    for (int i = 0; i < 6; ++i)
        assert(styles[i] == (i < 3 ? 0 : 1));
    reset();
    text("1234567890 lastword");
    assert(TALK_0f32_ProcessChar(0x82) == 1);
    assert(!strcmp(output, "1234567890 \nlastword"));
    reset();
    text("abcdefghijklmnopqrst");
    assert(TALK_0f32_ProcessChar(0x82) == 1);
    char letters[32];
    int count = 0;
    for (int i = 0; i < length; ++i)
        if (output[i] != '\n')
            letters[count++] = output[i];
    letters[count] = 0;
    assert(!strcmp(letters, "abcdefghijklmnopqrst"));
    reset();
    D_5893_map_id = 13;
    D_bcdc = 5;
    memset(D_5bda, 0, sizeof(D_5bda));
    D_5888 = 10;
    text("before branch");
    assert(TALK_0f32_ProcessChar(0x8c) == 0);
    assert(!strcmp(output, "before branch"));
    assert(TALK_0f32_ProcessChar(0x89) == 0); /* Label operand, not another action. */
    assert(D_5888 == 10 && !D_4aee && !strcmp(output, "before branch"));
    reset();
    assert(TALK_0f32_ProcessChar(1) == 0);
    assert(TALK_0f32_ProcessChar(0x82) == 1);
    assert(!strcmp(output, D_24ea[0]));
    /* Workshop's mixed paragraphs are ordinary native text and quote bytes. */
    reset();
    TALK_04da_PrintQuoteMark();
    text("I speak.\"");
    TALK_04d2_PrintCr();
    TALK_04d2_PrintCr();
    text("He gestures.");
    TALK_04d2_PrintCr();
    TALK_04d2_PrintCr();
    text("\"Listen.\"");
    /* Normal spoken dispatch supplies another closing quote; the native guard suppresses it. */
    TALK_04da_PrintQuoteMark();
    TALK_04e2_PrintWord();
    assert(!strcmp(output, "\"I speak.\"\n\nHe gestures.\n\n\"Listen.\""));
    /* Use the real game loader and interpreter with responses beyond 64 KiB.
     * Long names need not render to test selection past their full byte span. */
    const size_t padding = 70000;
    const unsigned char suffix[] = {0, 'A' | 128, 0, 'H' | 128, 'i' | 128, 130, 0, 'W' | 128,
                                    0, 'B' | 128, 0, 144,       159,       192, 0};
    size_t conversationSize = padding + sizeof(suffix);
    size_t bankSize = 24 + conversationSize;
    uint8_t *bank = calloc(1, bankSize);
    assert(bank);
    memcpy(bank, DIALOGUE_BANK_MAGIC, 8);
    bank[8] = 1;
    bank[12] = 1;
    bank[16] = 24;
    for (int i = 0; i < 4; ++i)
        bank[20 + i] = conversationSize >> (i * 8);
    memset(bank + 24, 'X' | 128, padding);
    memcpy(bank + 24 + padding, suffix, sizeof(suffix));
    FILE *large = fopen("dialogue-long-test.tlk", "wb");
    assert(large && fwrite(bank, 1, bankSize, large) == bankSize);
    assert(!fclose(large));
    uint8_t *loaded = TALK_LoadConversation("dialogue-long-test.tlk", 1);
    assert(loaded && s_conversationSize == conversationSize);
    reset();
    ImperaTalk session = nativeTalk();
    IMPERA_TalkSelect(&session, 2);
    assert(!session.error && *session.cursor == session.data + padding + 3);
    while (**session.cursor && !session.error)
        if (IMPERA_TalkCharacter(&session, *(*session.cursor)++))
            break;
    IMPERA_TalkFlush(&session);
    assert(!session.error && !strcmp(output, "Hi"));
    s_conversation = NULL;
    s_conversationSize = 0;
    D_bcde = NULL;
    free(loaded);
    assert(!TALK_LoadConversation("dialogue-long-test.tlk", 2));
    assert(!remove("dialogue-long-test.tlk"));
    free(bank);
    puts("PASS dialogue flushes literal/dictionary words before actions, preserves operands and "
         "rune style, and retains long words");
    return 0;
}
