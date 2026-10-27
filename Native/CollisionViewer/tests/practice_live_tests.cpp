#include "../src/vulkan_layer.cpp"
static void expect(bool value,const char* message) { if(!value) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
template<class T> static void put(void* at,size_t offset,T value) { memcpy(static_cast<unsigned char*>(at)+offset,&value,sizeof(value)); }
static std::vector<unsigned char> unhex(const char* text) {
    std::vector<unsigned char> bytes; auto value=[](char c) { return c<='9'?c-'0':c-'a'+10; };
    for(size_t i=0;i<strlen(text);i+=2) bytes.push_back(static_cast<unsigned char>((value(text[i])<<4)|value(text[i+1]))); return bytes;
}
static void transactionTests() {
    const std::string owner="76561197960275729";
    // Independent .NET AES-GCM fixtures, with a second nonce set for bytewise
    // transaction/rollback checks. These contain synthetic public test text.
    PracticeFiles first{
        unhex("0f5147d293a408342308436376f57b8a44e9c56a807f190de9a803d82e4657ba3b48951a279b70ea23c54091043bc78fc20390210419ef"),
        unhex("74da21ca004d50e8a08e85181984e45ec63f4b1368b059b7547b84e24e37f8a4fb53bca743aa4011b98f196e7acd43a7b49044591395e8d23567704e2c92"),
        unhex("0e51ce9c59f1f4f39e79776f91386b5abb6b038389df81a5e3535d72e3b91a77d9bd59fe3f83f179f9d667923fb7f0ae433cfca390445a53bcc590e7"),
        unhex("c51e333db8af2f163fe1d8caba0a4b0792da24448d4526a106bd3a980c0dd48aa0b934bf9b595ebef404f1305174d642a5cb9c888d1a5e63d41c29e61d34e608a47732")};
    PracticeFiles second{
        unhex("cd5b0e639fcf62a29438251aae8dddc21f1ffffb0f17f70767a586e191b718e49a382c6b386fa86269dab35416847a0c366738932cb8df"),
        unhex("ed035e18efa04b1d25a755529687993e5347d952655fbc3381be991d0f59d615fbc6fa73f538edceeaafa692a94fc211b4222900048bece29fee55e97e7e"),
        unhex("916919a92755e721d13b218769c011fb992fc61953b2eb0f7113963adb306c4d18664326411ca2bf7fbd4350b92083bd4f4a03930eac2c212e51f098"),
        unhex("5c57d328df651106c28cc65ee2e385ff77cd0a9f5108c43b94737b9a3e3debabaf73c67c460dfd19bceb795ef64dd58ab11e01b14b73c602d8b9a66ea76526cae6e054")};
    practiceAuthenticate(first,owner); practiceAuthenticate(second,owner);
    expect(practiceHash({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 known vector");
    expect(!practiceAuthentic(first[0],"76561197960275730",practiceFileKeys[0]),"foreign Steam owner rejected natively");
    expect(!practiceAuthentic(first[1],owner,practiceFileKeys[0]),"backup filename cannot be relabelled primary");
    auto damaged=first[0]; damaged.back()^=1; expect(!practiceAuthentic(damaged,owner,practiceFileKeys[0]),"tampered AES-GCM tag rejected");
    auto root=std::filesystem::temp_directory_path()/(L"DEViL-live-transaction-fixture-"+std::to_wstring(GetCurrentProcessId()));
    expect(std::filesystem::create_directory(root),"fresh transaction fixture directory");
    auto remote=root/L"Steam"/L"userdata"/L"10001"/L"782330"/L"remote",target=remote/L"DLC1-AUTOSAVE5",bundle=root/L"pending";
    std::filesystem::create_directories(bundle); writePracticeFiles(target,first);
    std::filesystem::create_directory(remote/L"PROFILE"); { std::ofstream file(remote/L"PROFILE"/L"profile.bin"); file<<"profile sentinel"; }
    { std::ofstream file(remote.parent_path()/L"remotecache.vdf"); file<<"index sentinel"; }
    PracticeRestorePlan plan; plan.archive=std::string(32,'a');
    for(size_t i=0;i<4;++i) { plan.targets[i]={first[i].size(),practiceHash(first[i])}; plan.sources[i]={second[i].size(),practiceHash(second[i])}; }
    {
        std::ofstream output(bundle/L"plan.tsv",std::ios::binary);
        output<<"#DEVIL_LIVE_RESTORE_PLAN_V1\t"<<owner<<"\tDLC1-AUTOSAVE5\t"<<std::string(32,'e')<<'\t'<<GetCurrentProcessId()<<'\t'<<plan.archive<<'\n';
        for(const auto& entries:{plan.targets,plan.sources}) for(size_t i=0;i<4;++i) output<<practiceFileKeys[i]<<'\t'<<entries[i].length<<'\t'<<entries[i].hash<<'\n'; output.close();
        auto parsed=readPracticeRestorePlan(bundle,owner,std::string(32,'e'),GetCurrentProcessId());
        expect(practiceTargetMatches(first,parsed)&&practiceFilesMatch(second,parsed.sources),"native restore plan pins both file sets");
        bool rejected=false; try { readPracticeRestorePlan(bundle,owner,std::string(32,'e'),GetCurrentProcessId()+1); } catch(...) { rejected=true; } expect(rejected,"restore plan process mismatch blocked");
    }
    int writes=0; PracticeStorageApi api;
    auto diskWrite=[&](size_t i,const std::vector<unsigned char>& bytes) { ++writes; std::ofstream file(target/practiceFileNames[i],std::ios::binary|std::ios::trunc); file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); return bool(file); };
    api.write=diskWrite; api.read=[&] { return readPracticeFiles(target); };
    auto reject=[&](const PracticeFiles& incoming,const PracticeRestorePlan& selected,const std::string& token,const PracticeStorageApi& storage,const std::function<bool()>& idle,const std::function<void(const char*)>& step={}) {
        bool failed=false; try { commitPracticeRestore(remote,bundle,incoming,first,selected,owner,token,storage,idle,step); } catch(const std::exception&) { failed=true; } expect(failed,"expected restore transaction rejection");
    };
    auto bad=plan; bad.targets[0].hash=std::string(64,'0'); reject(second,bad,std::string(32,'1'),api,[] { return true; }); expect(writes==0,"changed reviewed target was not written");
    bad=plan; bad.sources[0].hash=std::string(64,'0'); reject(second,bad,std::string(32,'0'),api,[] { return true; }); expect(writes==0,"changed reviewed incoming archive was not written");
    auto corrupt=second; corrupt[0].back()^=1; reject(corrupt,plan,std::string(32,'2'),api,[] { return true; }); expect(writes==0,"corrupt archive was not written");
    reject(second,plan,std::string(32,'3'),api,[] { return false; }); expect(writes==0,"busy engine did not write");
    auto stale=api; stale.read=[&] { return second; }; reject(second,plan,std::string(32,'4'),stale,[] { return true; }); expect(writes==0,"mismatched Steam cache rejected before writing");
    auto unavailable=api; unavailable.write=[](size_t,const std::vector<unsigned char>&) { return false; };
    std::string failure; try { commitPracticeRestore(remote,bundle,second,first,plan,owner,std::string(32,'9'),unavailable,[] { return true; }); } catch(const std::exception& error) { failure=error.what(); }
    expect(failure=="Steam checkpoint write failed."&&readPracticeFiles(target)==first,"failed write with unchanged verified original is not a rollback failure");
    int calls=0; auto failing=api; failing.write=[&](size_t i,const std::vector<unsigned char>& bytes) { if(++calls==2) return false; return diskWrite(i,bytes); };
    reject(second,plan,std::string(32,'5'),failing,[] { return true; }); expect(readPracticeFiles(target)==first,"partial API failure rolls all four originals back");
    auto cached=api; cached.read=[&] { return first; }; reject(second,plan,std::string(32,'6'),cached,[] { return true; }); expect(readPracticeFiles(target)==first,"post-write stale cache rolls original back");
    bool idle=true; reject(second,plan,std::string(32,'7'),api,[&] { return idle; },[&](const char* step) { if(strcmp(step,"backed_up")==0) idle=false; }); expect(readPracticeFiles(target)==first,"state change after backup leaves target intact");
    auto result=commitPracticeRestore(remote,bundle,second,first,plan,owner,std::string(32,'8'),api,[] { return true; });
    expect(readPracticeFiles(target)==second&&readPracticeFiles(result.original)==first,"successful commit retains exact original and installs complete incoming files");
    expect(std::filesystem::exists(result.journal),"restore journal preserved");
    std::string text; { std::ifstream file(remote/L"PROFILE"/L"profile.bin"); std::getline(file,text); } expect(text=="profile sentinel","PROFILE untouched");
    { std::ifstream file(remote.parent_path()/L"remotecache.vdf"); std::getline(file,text); } expect(text=="index sentinel","no direct Steam metadata overwrite");
    auto expectedRoot=std::filesystem::temp_directory_path()/(L"DEViL-live-transaction-fixture-"+std::to_wstring(GetCurrentProcessId()));
    expect(std::filesystem::weakly_canonical(root)==std::filesystem::weakly_canonical(expectedRoot)&&root.filename()==expectedRoot.filename(),"verified fixture cleanup boundary"); std::filesystem::remove_all(root);
    puts("PASS: independent native AES-GCM/SHA256, foreign/tampered/renamed rejection; reviewed target/cache/idle guards, four-file rollback, verified commit and retained originals/profile/index.");
}
struct Fixture {
    unsigned char* image=nullptr; uintptr_t base=0,system=0;
    std::vector<unsigned char> map=std::vector<unsigned char>(0x1c00),player=std::vector<unsigned char>(0x48000),ghost=std::vector<unsigned char>(0x80),users=std::vector<unsigned char>(0x30),record=std::vector<unsigned char>(0x100);
    std::array<uintptr_t,3> masterShared{},slotShared{}; std::array<unsigned char,16> master{}; uintptr_t list=0;
    Fixture() {
        image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x47cc000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)); expect(image!=nullptr,"fixture memory"); base=reinterpret_cast<uintptr_t>(image); system=base+0x45d82c0;
        put(image,kProfile6663.gameSystemRva,system); put(reinterpret_cast<void*>(system),0x44,uint32_t(2)); put(reinterpret_cast<void*>(system),0x50,reinterpret_cast<uintptr_t>(map.data()));
        put(map.data(),0x19c8,uint32_t(1)); put(map.data(),0x20,reinterpret_cast<uintptr_t>("game/dlc/e4m1_rig/e4m1_rig"));
        put(map.data(),kProfile6663.mapPlayerOffset,reinterpret_cast<uintptr_t>(player.data())); put(player.data(),0,base+kProfile6663.playerVtableRva);
        put(player.data(),kProfile6663.playerHudOffset+kProfile6663.hudPausedOffset,uint8_t(1));
        put(reinterpret_cast<void*>(system),0x9b38,reinterpret_cast<uintptr_t>(ghost.data())); put(image,0x47cb468,reinterpret_cast<uintptr_t>(users.data()));
        put(master.data(),4,uint32_t(42)); masterShared[1]=reinterpret_cast<uintptr_t>(master.data()); put(users.data(),0x18,reinterpret_cast<uintptr_t>(masterShared.data()));
        put(record.data(),0,uint32_t(42)); put(record.data(),8,uint32_t(2)); put(record.data(),24,reinterpret_cast<uintptr_t>("AUTOSAVE5"));
        slotShared[1]=reinterpret_cast<uintptr_t>(record.data()); list=reinterpret_cast<uintptr_t>(slotShared.data());
        put(ghost.data(),0x70,reinterpret_cast<uintptr_t>(&list)); put(ghost.data(),0x78,uint64_t(1));
    }
    PracticeRuntime read() { return readPracticeRuntime(WallMemory{},base,&kProfile6663); }
    ~Fixture() { VirtualFree(image,0,MEM_RELEASE); }
};
int main(int argc,char** argv) {
    if(argc==6&&strcmp(argv[1],"--plan")==0) {
        try {
            auto plan=readPracticeRestorePlan(std::filesystem::u8path(argv[2]),argv[3],argv[4],static_cast<DWORD>(std::stoul(argv[5])));
            auto incoming=readPracticeFiles(std::filesystem::u8path(argv[2])/L"incoming"); practiceAuthenticate(incoming,argv[3]);
            expect(practiceFilesMatch(incoming,plan.sources),"managed incoming files match native plan hashes");
            puts("PASS: managed staged plan accepted by production native parser, four incoming SHA-256/AES-GCM verified. No game/Steam calls or live save changes."); return 0;
        } catch(const std::exception& error) { fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
    }
    transactionTests();
    expect(practiceCloudDisabled(0,0),"Cloud disabled globally and per app");
    expect(practiceCloudDisabled(0,1),"global Cloud disable overrides enabled per-app preference");
    expect(practiceCloudDisabled(1,0),"per-app Cloud disable overrides global enable");
    expect(!practiceCloudDisabled(1,1),"Cloud enabled at both levels blocks live operations");
    expect(!practiceCloudDisabled(-1,0)&&!practiceCloudDisabled(0,-1)&&!practiceCloudDisabled(2,0),"unknown/invalid Cloud state blocked");
    Fixture f; expect(bool(f.read()),"idle paused TAG1 slot 3 accepted");
    put(f.record.data(),8,uint32_t(1)); expect(!f.read(),"wrong slot index blocked"); put(f.record.data(),8,uint32_t(2));
    put(f.record.data(),24,reinterpret_cast<uintptr_t>("AUTOSAVE4")); expect(!f.read(),"wrong slot name blocked"); put(f.record.data(),24,reinterpret_cast<uintptr_t>("AUTOSAVE5"));
    put(f.map.data(),0x20,reinterpret_cast<uintptr_t>("game/dlc/e5m1_spear/e5m1_spear")); expect(!f.read(),"other DLC blocked"); put(f.map.data(),0x20,reinterpret_cast<uintptr_t>("game/dlc/e4m1_rig/e4m1_rig"));
    auto hud=kProfile6663.playerHudOffset+kProfile6663.hudPausedOffset;
    put(f.player.data(),hud,uint8_t(0)); expect(!f.read(),"running game blocked"); put(f.player.data(),hud,uint8_t(1));
    put(reinterpret_cast<void*>(f.system),0x9ca8,uint32_t(1)); expect(!f.read(),"pending save blocked"); put(reinterpret_cast<void*>(f.system),0x9ca8,uint32_t(0));
    put(reinterpret_cast<void*>(f.system),0xb8,uint8_t(1)); expect(!f.read(),"map transition blocked"); put(reinterpret_cast<void*>(f.system),0xb8,uint8_t(0));
    std::array<uintptr_t,3> task{0,1,0}; put(f.ghost.data(),0x60,reinterpret_cast<uintptr_t>(task.data())); expect(!f.read(),"async load blocked"); put(f.ghost.data(),0x60,uintptr_t(0));
    std::array<uintptr_t,2> jobs{0,1}; task[1]=reinterpret_cast<uintptr_t>(jobs.data()); put(f.ghost.data(),0x58,reinterpret_cast<uintptr_t>(task.data())); expect(!f.read(),"active writer blocked"); jobs[1]=0; expect(bool(f.read()),"completed writer queue accepted");
    put(f.master.data(),4,uint32_t(43)); expect(!f.read(),"missing master user blocked"); put(f.master.data(),4,uint32_t(42));
    expect(!readPracticeRuntime(WallMemory{},f.base,&kProfile66632),"other game revision blocked");
    expect(practiceRemote(L"C:\\Steam\\userdata\\10001\\782330\\remote",76561197960275729ULL),"matching Steam folder owner accepted");
    expect(!practiceRemote(L"C:\\Steam\\userdata\\10002\\782330\\remote",76561197960275729ULL),"foreign folder blocked");
    expect(!practiceRemote(L"C:\\Steam\\userdata\\10001\\782330\\..\\remote",76561197960275729ULL),"path traversal blocked");
    expect(!practiceToken("../../escape")&&practiceToken(std::string(32,'a')),"bounded opaque request token");
    bool transition=false;
    expect(!practiceReloadCompleted(transition,100,true,2,100,true,true)&&!transition,"queued load is not a map transition");
    expect(!practiceReloadCompleted(transition,100,true,2,100,false,true),"queue clearing is not completion");
    expect(!practiceReloadCompleted(transition,100,true,1,100,true,false)&&transition,"loading state observed");
    expect(!practiceReloadCompleted(transition,100,true,2,100,false,false),"missing/wrong loaded map is not completion");
    expect(practiceReloadCompleted(transition,100,true,2,100,false,true),"returned player/map completes observed reload");
    auto temporary=std::filesystem::temp_directory_path()/(L"DEViL-live-fixture-"+std::to_wstring(GetCurrentProcessId())); std::filesystem::create_directory(temporary);
    const wchar_t* names[]{L"game.details",L"game.details-BACKUP",L"game_duration.dat",L"game_duration.dat-BACKUP"};
    for(auto name:names) { std::ofstream out(temporary/name,std::ios::binary); out<<std::string(28,'x'); }
    expect(readPracticeFiles(temporary)[0].size()==28,"complete checkpoint copy");
    HANDLE writer=CreateFileW((temporary/names[0]).c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr); expect(writer!=INVALID_HANDLE_VALUE,"fixture writer");
    bool blocked=false; try { readPracticeFiles(temporary); } catch(...) { blocked=true; } expect(blocked,"concurrent writer denied"); CloseHandle(writer);
    { std::ofstream extra(temporary/L"unexpected.dat"); extra<<"x"; } blocked=false; try { readPracticeFiles(temporary); } catch(...) { blocked=true; } expect(blocked,"unsupported extra file blocked");
    std::filesystem::remove_all(temporary);
    puts("PASS: live practice slot/profile/pause/user/writer/map guards, account-bound paths, bounded tokens, complete file read and concurrent-writer rejection. No engine/Steam calls executed.");
}
