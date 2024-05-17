#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include <sstream>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <libtorrent/session.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <chrono>
#include <libtorrent/torrent_status.hpp>
#include <thread>

using namespace std;

const string USER = getenv("USER");
const string config_file_path = "/home/" + USER + "/.config/virt-cli.conf";

struct conf
{
    string iso_folder_path;
    string disk_folder_path;
};

conf read_or_prompt_for_config(const string &config_file_path)
{
    conf config;
    ifstream config_file(config_file_path);

    if (!config_file)
    {
        cout << "Config file not found. Creating Config file.\n";
    }

    if (config_file)
    {
        std::getline(config_file, config.iso_folder_path);
        std::getline(config_file, config.disk_folder_path);
    }

    if (config.iso_folder_path.empty())
    {
        cout << "Enter the path to the ISO folder: ";
        cin >> config.iso_folder_path;
    }

    if (config.disk_folder_path.empty())
    {
        cout << "Enter the path to the disk folder: ";
        cin >> config.disk_folder_path;
    }

    ofstream out_config_file(config_file_path);
    out_config_file << config.iso_folder_path << '\n' << config.disk_folder_path << '\n';

    return config;
}

size_t write_data(void* ptr, size_t size, size_t nmemb, FILE* stream)
{
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

enum class OS
{
    ubuntu,
    debian,
    arch
};

bool is_url_active(const string &url)
{
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // send HEAD request
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // return error if HTTP response is >= 400
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        return (res == CURLE_OK);
    }
    return false;
}

vector<string> debian_iso_urls =
{
    "https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-12.4.0-amd64-netinst.iso",
    "https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-10.9.0-amd64-netinst.iso",
    "https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-10.9.0-amd64-netinst.iso",
};

string check_iso_urls(const vector<string> &iso_urls)
{
    for (const string &url : iso_urls)
    {
        if (is_url_active(url))
        {
            return url;
        }
        else
        {
            cout << "The ISO file cannot be downloaded from: " << url << '\n';
        }
    }

    return "";
}

string fetch_iso_url(OS os)
{
    switch(os)
    {
        case OS::ubuntu:
        {
            return "";
        } 
            
        case OS::debian:
        {
            return check_iso_urls(debian_iso_urls);
        } 
            
        case OS::arch:
        {
            return "";  
        } 
        
        default:
        {
            return "";
        }
    }
}

void download_iso(const string &url, const string &output_file_path)
{
    CURL* curl;
    FILE* fp;
    CURLcode res;

    curl = curl_easy_init();
    if (curl)
    {
        fp = fopen(output_file_path.c_str(), "wb");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        res = curl_easy_perform(curl);
        /* Check for errors */ 
        if(res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        /* always cleanup */ 
        curl_easy_cleanup(curl);
        fclose(fp);
    }
}

int download_iso_torrent(string magnet_link)
{
    lt::session s;
    lt::add_torrent_params p;
    lt::error_code ec;
    lt::parse_magnet_uri(magnet_link, p, ec);
    if (ec)
    {
        cerr << "Error: " << ec.message() << endl;
        return 1;
    }

    p.save_path = "./";
    lt::torrent_handle h = s.add_torrent(p);

    while (!h.status().is_seeding)
    {
        this_thread::sleep_for(std::chrono::seconds(1));
        cout << "Progress: " << h.status().progress * 100 << "%" << endl;
    }

    cout << "Download completed" << endl;
    return 0;
}

vector<string> qemu_args =
{
    "qemu-system-x86_64",
    "--enable-kvm",
    "-name $VM_NAME",
    "-drive file=$DISK_IMAGE,format=qcow2",
    "-cdrom $ISO_FILE",
    "-m $RAM_SIZE",
    "-smp $CPU_CORES",
    "-cpu host",
    "-machine q35,accel=kvm",
    "-boot $BOOT_ORDER",
    "-drive file=$UEFI_BIOS,if=pflash,format=raw,unit=0,readonly=on",
    "-vnc :0",
    "-vnc 192.168.0.14:0",
    "-vnc 0.0.0.0:14" 
};

void launchQemuVM(const vector<string> & args)
{
    ostringstream cmd;
    cmd << "qemu-system-x86_64 ";

    for (const string &arg : args)
    {
        cmd << arg << ' ';
    }

    system(cmd.str().c_str());
}

void create_qcow2_disk(const string &disk_path, int size_gb)
{
    string command = "qemu-img create -f qcow2 " + disk_path + " " + to_string(size_gb) + "G";

    int result = system(command.c_str());
    if (result != 0)
    {
        cerr << "Failed to create disk image\n";
    }
}

void create_vm(const string &name, OS os)
{
    conf config = read_or_prompt_for_config("virt-cli.conf");
    string iso_url = fetch_iso_url(os);
    if (iso_url.empty())
    {
        cerr << "No valid ISO URL found. Aborting.\n";
        return;
    }

    system(("mkdir -p " + config.iso_folder_path).c_str());
    system(("wget " + iso_url + " -O " + config.iso_folder_path + "/" + name + ".iso").c_str());

    int size_gb;
    cout << "Enter the size of the disk image in GB: ";
    cin >> size_gb;

    system(("mkdir -p " + config.disk_folder_path).c_str());
    create_qcow2_disk(config.disk_folder_path + "/" + name, size_gb);
}

void create_arch_vm(const string &name, OS os)
{
    conf config = read_or_prompt_for_config("virt-cli.conf");
    system(("mkdir -p " + config.iso_folder_path).c_str());
    int status = download_iso_torrent("magnet:?xt=urn:btih:1447bb03de993e1ee7e430526ff1fbac0daf7b44&dn=archlinux-2024.01.01-x86_64.iso");

    int size_gb;
    cout << "Enter the size of the disk image in GB: ";
    cin >> size_gb;

    system(("mkdir -p " + config.disk_folder_path).c_str());
    create_qcow2_disk(config.disk_folder_path + "/" + name, size_gb);
}

string prompt_for_vm_name()
{
    string name;

    cout << "Enter the name of the VM: ";
    cin >> name;

    return name;
}

void start_vm(const string &name)
{
    conf config = read_or_prompt_for_config("virt-cli.conf");
    string boot_order;
    
    while (true)
    {
        int choice;
        cout << "Enter [1] for disk_boot, Enter [2] for iso_boot: ";
        cin >> choice;
        
        if (choice == 1)
        {
            boot_order = "order=c";
            break;
        }

        if (choice == 2)
        {
            boot_order = "order=d";
            break;
        } 
        
        cout << "Invalid choice. Please try again.\n";
    }

    vector<string> qemu_args =
    {
        "--enable-kvm",
        "-name " + name,
        "-drive file=" + config.disk_folder_path + "/" + name + ",format=qcow2",
        "-cdrom " + config.iso_folder_path + "/" + name + ".iso",
        "-m 8G",
        "-smp 12",
        "-cpu host",
        "-machine q35,accel=kvm",
        "-boot " + boot_order,
        "-drive file=/usr/share/edk2/x64/OVMF_CODE.fd,if=pflash,format=raw,unit=0,readonly=on",
        "-vnc :0",
        "-vnc 192.168.0.14:0",
        "-vnc 0.0.0.0:14" 
    };

    launchQemuVM(qemu_args);
}

struct MenuOption
{
    string name;
    function<void()> action;
};

void display_menu(const vector<MenuOption> &options)
{
    while (true)
    {
        cout << "\nMenu:\n";

        for (size_t i = 0; i < options.size(); ++i)
        {
            cout << i + 1 << ". " << options[i].name << '\n';
        }

        cout << "Enter the number of your choice, or 0 to go back: ";

        int choice;
        cin >> choice;

        if (choice == 0)
        {
            break;
        }

        if (choice > 0 && choice <= options.size())
        {
            options[choice - 1].action();
            break;
        }

        cout << "Invalid choice. Please try again.\n";
    }
}

vector<MenuOption> new_vm =
{
    {"debian", []()
    {
        create_vm(prompt_for_vm_name(), OS::debian);
    }},

    {"ubuntu", []()
    { 
        cout << "You chose sub-option 2.\n"; 
    }},

    {"arch_linux", []()
    {
        create_arch_vm(prompt_for_vm_name(), OS::arch);
    }}
};

vector<MenuOption> vm_menu =
{
    {"Start VM", []()
    {
        start_vm(prompt_for_vm_name());
    }},
    
    {"Stop VM", []()
    {
        cout << "You chose sub-option 2.\n";
    }},

    {"Delete VM", []() {
        cout << "You chose sub-option 3.\n";
    }},

    {"New VM", []()
    {
        display_menu(new_vm);
    }},
};

vector<MenuOption> disk_menu =
{
    {"Create new disk", []()
    {
        conf config = read_or_prompt_for_config("virt-cli.conf");
        string disk_name;
        int size_gb;

        cout << "Enter the Name to the disk image: ";
        getline(cin, disk_name);

        cout << "Enter the size of the disk image in GB: ";
        cin >> size_gb;

        create_qcow2_disk(config.disk_folder_path + "/" + disk_name, size_gb);
    }},

    {"Delete disk", []()
    {
        cout << "You chose sub-option 2.\n";
    }},
};

vector<MenuOption> main_menu =
{
    {"check config", []()
    {
        conf config = read_or_prompt_for_config("virt-cli.conf");
        cout << "iso folder path: " << config.iso_folder_path << '\n';
        cout << "disk folder path: " << config.disk_folder_path << '\n';
    }},

    {"vm_menu",  []()
    {
        display_menu(vm_menu);
    }},

    {"disk_menu", []()
    {
        display_menu(disk_menu);
    }},
};

int main()
{
    display_menu(main_menu);
    return 0;
}