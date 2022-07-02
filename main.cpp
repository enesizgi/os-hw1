#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include "fat32.h"
#include "parser.h"

using namespace std;

uint32_t current_dir_cluster_no;
uint32_t beginning_of_clusters;
uint32_t bytes_per_cluster;
uint16_t beginning_of_fat_table;
uint32_t end_of_chain;

string months_str[] = {"January", "February", "March", "April", "May", "June","July","August","September","October","November","December"};

uint32_t get_fat_entry_at (FILE*& imgFile, uint32_t location);
vector<uint32_t>* find_entries (uint32_t& first_cluster, FILE*& imgFile, BPB_struct& BPBstruct);
string lfn_name_extract (FatFileEntry*& file_entry);

vector<FatFileEntry> get_fatFileEntries_in_cluster(uint32_t cluster_no, FILE*& imgFile, BPB_struct& BPBstruct) {
    fseek(imgFile, beginning_of_clusters + (cluster_no - BPBstruct.extended.RootCluster) * bytes_per_cluster, SEEK_SET);
    int size_of_fatFileEntry = sizeof(FatFileEntry);
    vector<FatFileEntry> FatFileEntries;
    for (int j = 0; j < bytes_per_cluster; j += size_of_fatFileEntry) {
        auto *tmp_file = new FatFileEntry;
        fread(tmp_file, size_of_fatFileEntry, 1, imgFile);
        FatFileEntries.push_back(*tmp_file);
        delete tmp_file;
    }
    return FatFileEntries;
}

bool is_path_valid (vector<string>& path, FILE*& imgFile, BPB_struct& BPBstruct, bool is_file = false, FatFileEntry* file_entry_83 = nullptr, bool shouldClusterNoChange = true) {
    if (path.empty()) return false;

    uint32_t nextCluster = BPBstruct.extended.RootCluster;
    size_t pathSize = path.size();
    for (int i = 0; i < pathSize - 1; i++) {
        auto* root_fat_entries_p = find_entries(nextCluster, imgFile, BPBstruct);
        auto& root_fat_entries = *root_fat_entries_p;
        bool is_folder_found = false;
        string FatFileEntryName;
        bool is_deleted = false;
        for (unsigned int root_fat_entrie : root_fat_entries) {
            auto FatFileEntries = get_fatFileEntries_in_cluster(root_fat_entrie, imgFile, BPBstruct);
            for (auto & FatFileEntrie : FatFileEntries) {
                if (FatFileEntrie.lfn.attributes == 15) {
                    FatFileEntry* tmp = &FatFileEntrie;
                    FatFileEntryName.insert(0, lfn_name_extract(tmp));
                    if (FatFileEntrie.lfn.sequence_number == 0xE5) is_deleted = true;
                }
                else {
                    if (pathSize - 2 == i && is_file && path[i+1] == FatFileEntryName) { // It is a file.
                        is_folder_found = true;
                        nextCluster = FatFileEntrie.msdos.firstCluster;
                        if (file_entry_83 != nullptr) {
                            *file_entry_83 = FatFileEntrie;
                        }
                    }
                    else if ((FatFileEntrie.msdos.attributes & 0x10) == 0x10 && path[i+1] == FatFileEntryName) { // It is a directory.
                        is_folder_found = true;
                        nextCluster = FatFileEntrie.msdos.firstCluster;
                    }
                    is_deleted = false;
                    FatFileEntryName.clear();
                }
            }
            if (is_folder_found) break;
        }
        delete root_fat_entries_p;
        if (!is_folder_found) return false;
    }
    if (shouldClusterNoChange) current_dir_cluster_no = nextCluster;
    return true;
}

void split_path(vector<string>& result, string& path, string& current_working_dir) {
    string s = path.substr(0,1) == "/" ? path + "/" : current_working_dir + (current_working_dir != "/" ? "/" : "") + path + "/";
    s = path == "/" ? path : s;
    std::string delimiter = "/";

    size_t pos = 0;
    std::string token;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);
        result.push_back(token);
        s.erase(0, pos + delimiter.length());
    }
}

void clean_path (vector<string>& clean_result, vector<string>& dirty_path) {
    for (auto & i : dirty_path) {
        if (i.empty()) {
            clean_result.push_back(i);
        }
        else if (i == ".") {
            continue;
        }
        else if (i == "..") {
            // TODO: MAYBE CHECK HERE IF CLEAN_RESULT IS EMPTY (IT SHOULDN'T BE EMPTY EVER)
            string tmp = clean_result.back();
            if (tmp.empty()) {
                continue;
            }
            clean_result.pop_back();
        }
        else clean_result.push_back(i);
    }
}

void concat_string (string& str, vector<string>& strings) {
    for (int i = 0; i < strings.size(); i++) {
        if (i == strings.size() - 1 && strings.size() != 1) {
            str += strings[i];
        }
        else str += strings[i] + "/";
    }
}

string resolve_path (string& path, string& current_working_dir, FILE*& imgFile, BPB_struct& BPBstruct) {
    vector<string> result;
    split_path(result, path, current_working_dir);
    if (result.empty()) {
        return current_working_dir;
    }

    vector<string> clean_result;
    clean_path(clean_result, result);

    if (!is_path_valid(clean_result, imgFile, BPBstruct)) {
        return current_working_dir;
    }

    string s_tmp;
    concat_string(s_tmp, clean_result);
    return s_tmp;
}

string lfn_name_extract (FatFileEntry*& file_entry) {
    string result;
    for (auto& i : file_entry->lfn.name1) {
        if (i != 0) result += (char)i;
        else return result;
    }
    for (auto& i : file_entry->lfn.name2) {
        if (i != 0) result += (char)i;
        else return result;
    }
    for (auto& i : file_entry->lfn.name3) {
        if (i != 0) result += (char)i;
        else return result;
    }
    return result;
}

uint32_t get_fat_entry (FILE*& imgFile) {
    vector<uint8_t> fat_entry;
    auto* tmp = new uint8_t;
    for (int i = 0; i < 4; i++) {
        fread(tmp, sizeof(uint8_t), 1, imgFile);
        fat_entry.push_back(*tmp);
    }
    delete tmp;
    uint32_t tmp2 = (fat_entry[3]<<24) + (fat_entry[2]<<16) + (fat_entry[1]<<8) + fat_entry[0];
    return tmp2;
}

uint32_t get_fat_entry_at (FILE*& imgFile, uint32_t location) {
    fseek(imgFile, location, SEEK_SET);
    return get_fat_entry(imgFile);
}

vector<uint32_t>* find_entries (uint32_t& first_cluster, FILE*& imgFile, BPB_struct& BPBstruct) {
    fseek(imgFile, beginning_of_fat_table+8+(first_cluster - BPBstruct.extended.RootCluster)*4, SEEK_SET);
    auto* fat_entries_p = new vector<uint32_t>;
    auto& fat_entries = *fat_entries_p;
    fat_entries.push_back(first_cluster);
    auto* tmp_entry = new uint32_t; // TODO: NEED TO TEST WITH CHAIN FOLDERS. THIS IS PROBABLY WRONG BECAUSE OF LITTLE ENDIAN. SHOULD USE GET_FAT_ENTRY
    while (true) {
        fread(tmp_entry, sizeof(uint32_t), 1, imgFile);
        if (*tmp_entry == end_of_chain) break;
        else {
            fat_entries.push_back(*tmp_entry);
            fseek(imgFile, beginning_of_fat_table+4*(*tmp_entry), SEEK_SET);
        }
    }
    delete tmp_entry;
    return fat_entries_p;
}

uint32_t find_free_entry (FILE*& imgFile, BPB_struct& BPBstruct) {
    fseek(imgFile, beginning_of_fat_table+8, SEEK_SET);
    uint32_t cluster_no = BPBstruct.extended.RootCluster;
    while (true) {
        uint32_t entry = get_fat_entry(imgFile);
        if (entry == 0) return cluster_no;
        cluster_no++;
    }
}

uint32_t add_free_entry (uint32_t& first_cluster, FILE*& imgFile, BPB_struct& BPBstruct) {
    auto* root_fat_entries_p = find_entries(first_cluster, imgFile, BPBstruct);
    auto& root_fat_entries = *root_fat_entries_p;

    uint32_t tmp_cluster_no = root_fat_entries.back();
    fseek(imgFile, beginning_of_fat_table+8+(root_fat_entries.back() - BPBstruct.extended.RootCluster)*4, SEEK_SET);
    uint32_t new_cluster_no = find_free_entry(imgFile, BPBstruct);
    fwrite(&new_cluster_no, 4, 1, imgFile);

    fseek(imgFile, beginning_of_fat_table+8+(new_cluster_no - BPBstruct.extended.RootCluster)*4, SEEK_SET);
    fwrite(&end_of_chain, 4, 1, imgFile);

    return new_cluster_no;
}

void modify_time(FatFileEntry& msdos) {
   time_t now = time(nullptr);
   tm *ltm = localtime(&now);

   msdos.msdos.creationTime = msdos.msdos.modifiedTime = (ltm->tm_hour << 11) + (ltm->tm_min << 5);
   msdos.msdos.creationDate = msdos.msdos.modifiedDate = msdos.msdos.lastAccessTime = (ltm->tm_mon << 5) + ltm->tm_mday;
}

unsigned char lfn_checksum(const unsigned char *pFCBName)
{
   int i;
   unsigned char sum = 0;

   for (i = 11; i; i--)
      sum = ((sum & 1) << 7) + (sum >> 1) + *pFCBName++;

   return sum;
}

vector<FatFileEntry> create_lfns (string& filename) {
    string tmp_string(filename);
    vector<FatFileEntry> lfns;
    const auto* chr_filename = reinterpret_cast<const unsigned char *>(filename.c_str());
    auto checksum = lfn_checksum(chr_filename);

    while (!tmp_string.empty()) {
        FatFileEntry lfn;
        lfn.lfn.attributes = 15;
        lfn.lfn.firstCluster = 0;
        lfn.lfn.reserved = 0;
        lfn.lfn.checksum = checksum;
        for (int i = 0; i < 5; i++) {
            lfn.lfn.name1[i] = 65535;
        }
        for (int i = 0; i < 6; i++) {
            lfn.lfn.name2[i] = 65535;
        }
        for (int i = 0; i < 2; i++) {
            lfn.lfn.name3[i] = 65535;
        }
        // TODO: lfn.lfn.sequence_number
        for (int i = 0; i < 5; i++) {
            if (tmp_string.empty()) {
                lfn.lfn.name1[i] = 0;
                lfns.push_back(lfn);
                return lfns;
            }
            lfn.lfn.name1[i] = *tmp_string.substr(0,1).c_str();
            tmp_string.erase(0,1);
        }

        for (int i = 0; i < 6; i++) {
            if (tmp_string.empty()) {
                lfn.lfn.name2[i] = 0;
                lfns.push_back(lfn);
                return lfns;
            }
            lfn.lfn.name2[i] = *tmp_string.substr(0,1).c_str();
            tmp_string.erase(0,1);
        }

        for (int i = 0; i < 2; i++) {
            if (tmp_string.empty()) {
                lfn.lfn.name3[i] = 0;
                lfns.push_back(lfn);
                return lfns;
            }
            lfn.lfn.name3[i] = *tmp_string.substr(0,1).c_str();
            tmp_string.erase(0,1);
        }

        lfns.push_back(lfn);
    }
    return lfns;
}

FatFileEntry create_msdos (FatFileEntry& previous_msdos) {
    FatFileEntry f;
    f.msdos.firstCluster = f.msdos.fileSize = f.msdos.eaIndex = f.msdos.creationTimeMs = f.msdos.reserved = 0;
    f.msdos.attributes = 32;

    modify_time(f);

    for (unsigned char & i : f.msdos.extension) i = 32;

    f.msdos.filename[0] = 126;
    for (int i = 1; i < 8; i++) {
        f.msdos.filename[i] = 32;
    }

    uint8_t index = 0;
    string index_str;
    for (int i = 1; i < 8; i++) {
        if (previous_msdos.msdos.filename[i] == 32) break;
        else {
            char tmp_char = previous_msdos.msdos.filename[i];
            string tmp_string;
            tmp_string.push_back(tmp_char);
            index_str.append(tmp_string);
        }
    }

    index = index_str.empty() ? 0 : stoul(index_str, nullptr);
    index++;
    index_str = to_string(index);
    int size = index_str.size();
    for (int i = 1; i < 8; i++) {
        if (index_str.empty()) break;
        f.msdos.filename[i] = *index_str.substr(0,1).c_str();
        index_str.erase(0,1);
    }

    return f;
}

void cd_command (string& path, FILE*& imgFile, BPB_struct& BPBstruct, string& current_working_dir) {
    if (path.empty()) return;
    current_working_dir = resolve_path(path, current_working_dir, imgFile, BPBstruct);
}

void ls_command (parsed_input& input, FILE*& imgFile, BPB_struct& BPBstruct, string& current_working_dir) {
    // TODO: Should LS handle this: ls -l dir2file.c or ls file.c ? Right now, this is not printing anything.
    string arg1 = input.arg1 != nullptr ? input.arg1 : "";
    string arg2 = input.arg2 != nullptr ? input.arg2 : "";
    string backup_current_working_dir = current_working_dir;
    bool is_cd_used = false;

    if (!arg1.empty() && arg1 != "-l") {
        vector<string> result;
        split_path(result, arg1, current_working_dir);
        if (result.empty()) {
            return;
        }

        vector<string> clean_result;
        clean_path(clean_result, result);

        if (!is_path_valid(clean_result, imgFile, BPBstruct)) {
            return;
        }
        cd_command(arg1, imgFile, BPBstruct, current_working_dir);
        is_cd_used = true;
    }
    else if (arg1 == "-l" && !arg2.empty()) {
        vector<string> result;
        split_path(result, arg2, current_working_dir);
        if (result.empty()) {
            return;
        }

        vector<string> clean_result;
        clean_path(clean_result, result);

        if (!is_path_valid(clean_result, imgFile, BPBstruct)) {
            return;
        }

        cd_command(arg2, imgFile, BPBstruct, current_working_dir);
        is_cd_used = true;
    }

    auto* root_fat_entries_p = find_entries(current_dir_cluster_no, imgFile, BPBstruct);
    auto& root_fat_entries = *root_fat_entries_p;
    int print_counter = 0;
    string str1; // TODO: MAYBE SHOULD MOVE THIS TO A LEVEL UP OUTSIDE THIS FOR LOOP. (DONE)
    bool is_deleted = false;
    for (auto& root_fat_entry : root_fat_entries) {
        fseek(imgFile, beginning_of_clusters + (root_fat_entry - BPBstruct.extended.RootCluster)*bytes_per_cluster, SEEK_SET); // TODO: CHECK IF ROOTCLUSTER EDIT WORKS
        int size_of_fatFileEntry = sizeof(FatFileEntry);
        auto* tmp_file = new FatFileEntry; // TODO: Change this to stack allocation.
        for (int j = 0; j < bytes_per_cluster; j+=size_of_fatFileEntry) {
            fread(tmp_file, size_of_fatFileEntry, 1, imgFile);
            if (tmp_file->lfn.attributes == 15) { // It is LFN.
                str1.insert(0, lfn_name_extract(tmp_file));
                if (tmp_file->lfn.sequence_number == 0xE5) {
                    is_deleted = true;
                }
            }
            else {
                if (is_deleted) {
                    str1.clear();
                    is_deleted = false;
                }
                else if (str1.length() > 0) {
                    if (arg1 == "-l") {
                        uint8_t hours = (tmp_file->msdos.modifiedTime & 0xf800) >> 11;
                        uint8_t minutes = (tmp_file->msdos.modifiedTime & 0x7e0) >> 5;
                        string month = months_str[(tmp_file->msdos.modifiedDate & 0x1e0) >> 5];
                        uint8_t day = tmp_file->msdos.modifiedDate & 0x1f;
                        if ((tmp_file->msdos.attributes & 0x10) == 0x10) { // It is a directory.
                            if (j != 0 && print_counter != 0) cout << endl;
                            cout << "drwx------ 1 root root 0 " << month << " " << to_string(day) << (hours < 10 ? " 0" : " ");
                            cout << to_string(hours) << ":" << (minutes < 10 ? "0" : "") << to_string(minutes) << " " << str1;
                        }
                        else {
                            if (j != 0 && print_counter != 0) cout << endl;
                            cout << "-rwx------ 1 root root " << tmp_file->msdos.fileSize << " " << month << " " << to_string(day) << (hours < 10 ? " 0" : " ");
                            cout << to_string(hours) << ":" << (minutes < 10 ? "0" : "") << to_string(minutes) << " " << str1;
                        }
                    }
                    else {
                        if (j != 0 && print_counter != 0) cout << " ";
                        cout << str1;
                    }
                    print_counter++;
                    str1.clear();
                }
            }
        }
        delete tmp_file;
    }
    if (print_counter > 0) cout << endl;
    root_fat_entries.clear();
    delete root_fat_entries_p;

    if (is_cd_used) {
        cd_command(backup_current_working_dir, imgFile, BPBstruct, current_working_dir);
    }
}

void cat_command(parsed_input& input, FILE*& imgFile, BPB_struct& BPBstruct, string& current_working_dir) {
    if (input.arg1 == nullptr) return;
    string path(input.arg1);

    vector<string> result;
    split_path(result, path, current_working_dir);
    if (result.empty()) {
        return;
    }

    vector<string> clean_result;
    clean_path(clean_result, result);

    FatFileEntry file_entry_83;
    if (!is_path_valid(clean_result, imgFile, BPBstruct, true, &file_entry_83, false)) {
        return;
    }

    uint32_t first_entry = file_entry_83.msdos.firstCluster;
    auto* fat_entries_p = find_entries(first_entry, imgFile, BPBstruct);
    auto& fat_entries = *fat_entries_p;

    for (int i = 0; i < fat_entries.size(); i++) {
        char content[1024] = {0};
        fseek(imgFile, beginning_of_clusters + (fat_entries[i] - BPBstruct.extended.RootCluster) * bytes_per_cluster, SEEK_SET);
        fread(content, sizeof(content), 1, imgFile);
        cout << content;
    }
}

void touch_command(parsed_input& input, FILE*& imgFile, BPB_struct& BPBstruct, string& current_working_dir) {
    if (input.arg1 == nullptr) return;
    string path(input.arg1);

    vector<string> result;
    split_path(result, path, current_working_dir);
    if (result.empty()) {
        return;
    }

    vector<string> clean_result;
    clean_path(clean_result, result);

    if (is_path_valid(clean_result, imgFile, BPBstruct, true, nullptr, false)) return; // Checks if file exists, it shouldn't

    string filename = clean_result.back();
    clean_result.pop_back();
    if (!is_path_valid(clean_result, imgFile, BPBstruct, false, nullptr, false)) return; // Checks if directory exists, it should

    string backup_current_working_dir = current_working_dir;
    auto tmp_arg1 = input.arg1;
    string cd_directory;
    concat_string(cd_directory, clean_result);
    cd_command(cd_directory, imgFile, BPBstruct, current_working_dir);

    auto* fat_entries_p = find_entries(current_dir_cluster_no, imgFile, BPBstruct);
    auto& fat_entries = *fat_entries_p;

    
    int size_of_fatFileEntry = sizeof(FatFileEntry);
    FatFileEntry tmp_file;
    bool is_lfn = false;

    vector<FatFileEntry> file_entries = create_lfns(filename);
    FatFileEntry msdos;
    FatFileEntry previous_msdos;
    for (int i = 0; i < 8; i++) // TODO: For . and .. entries. It could be unnecessary in touch. Check this in mkdir.
        previous_msdos.msdos.filename[i] = 32;

    // TODO: Should we check for deleted files?
    for (int i = 0; i < fat_entries.size(); i++) {
        if (file_entries.empty()) break;
        fseek(imgFile, beginning_of_clusters + (fat_entries[i] - BPBstruct.extended.RootCluster) * bytes_per_cluster, SEEK_SET);
        for (int j = 0; j < bytes_per_cluster; j+=size_of_fatFileEntry) {
            if (file_entries.empty()) break;
            fread(&tmp_file, size_of_fatFileEntry, 1, imgFile);
            if (tmp_file.lfn.attributes == 15) {
                is_lfn = true;
            }
            else {
                if (is_lfn) {
                    is_lfn = false;
                    previous_msdos = tmp_file;
                }
                else { // We found the empty space for our file.
                    fseek(imgFile, -size_of_fatFileEntry, SEEK_CUR);
                    msdos = create_msdos(previous_msdos);
                    file_entries.insert(file_entries.begin(), msdos);
                    for (int k = 0; k < (bytes_per_cluster - j) / size_of_fatFileEntry; i++) {
                        if (file_entries.empty()) break;
                        fwrite(&file_entries.back(), size_of_fatFileEntry, 1, imgFile);
                        file_entries.pop_back();
                    }
                }
            }
        }
    }

    while (!file_entries.empty()) {
        uint32_t new_cluster_no = add_free_entry(current_dir_cluster_no, imgFile, BPBstruct);

        fseek(imgFile, beginning_of_clusters + (new_cluster_no - BPBstruct.extended.RootCluster) * bytes_per_cluster, SEEK_SET);
        for (int j = 0; j < bytes_per_cluster; j+=size_of_fatFileEntry) {
            if (file_entries.empty()) break;
            fwrite(&file_entries.back(), size_of_fatFileEntry, 1, imgFile);
            file_entries.pop_back();
        }
    }

    cd_command(backup_current_working_dir, imgFile, BPBstruct, current_working_dir);

}

int main(int argc, char *argv[]) {
    string current_working_dir = "/";
    bool is_development = true; // TODO: Change this to false before submission
    if (argc != 2) {
        exit(1);
    }

    string filename("/home/enes/os-hw3/");
    filename.append(argv[1]);

    FILE* imgFile;
    imgFile = fopen(is_development ? filename.c_str() : argv[1],"r+");
    if (imgFile == nullptr) {
        fclose(imgFile);
        exit(1);
    }

    BPB_struct BPBstruct;
    size_t result = fread(&BPBstruct, sizeof(BPB_struct), 1, imgFile);
    if (result != 1) {
        exit(1);
    }

    current_dir_cluster_no = BPBstruct.extended.RootCluster;
    bytes_per_cluster = BPBstruct.BytesPerSector*BPBstruct.SectorsPerCluster;
    beginning_of_clusters = (BPBstruct.ReservedSectorCount+BPBstruct.extended.FATSize*BPBstruct.NumFATs)*BPBstruct.BytesPerSector;
    beginning_of_fat_table = BPBstruct.ReservedSectorCount*BPBstruct.BytesPerSector;
    end_of_chain = get_fat_entry_at(imgFile, beginning_of_fat_table);

    while (true) {
        cout << current_working_dir << "> ";
        string line;
        std::getline(std::cin, line);

        if (line == "quit") {
            return 0;
        }

        if (line.length() == 0) continue;

        parsed_input input;
        char* input_chr = new char[line.size() + 1];
        strcpy(input_chr, line.c_str());
        parse(&input, input_chr);

        if (input.type == LS) {
            ls_command(input, imgFile, BPBstruct, current_working_dir);
        }
        else if (input.type == CD) {
            if (input.arg1 != nullptr) {
                string path(input.arg1);
                cd_command(path, imgFile, BPBstruct, current_working_dir);
            }
        }
        else if (input.type == CAT) {
            cat_command(input, imgFile, BPBstruct, current_working_dir);
        }
        else if (input.type == TOUCH) {
            touch_command(input, imgFile, BPBstruct, current_working_dir);
        }

        clean_input(&input);
        delete[] input_chr;
    }
}
