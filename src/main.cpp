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

template<typename T>
using Vec = std::vector<T>;
using Str = std::string;
const Str USER = getenv("USER");
const Str config_file_path = "/home/" + USER + "/.config/virt-cli.conf";

struct conf {
  Str iso_folder_path;
  Str disk_folder_path;
};

conf read_or_prompt_for_config(const Str & config_file_path) {
  conf config;
  std::ifstream config_file(config_file_path);

  if(!config_file) {
    std::cout << "Config file not found. Creating Config file.\n";
  }

  if(config_file) {
    std::getline(config_file, config.iso_folder_path);
    std::getline(config_file, config.disk_folder_path);
  }

  if(config.iso_folder_path.empty()) {
    std::cout << "Enter the path to the ISO folder: ";
    std::cin >> config.iso_folder_path;
  }

  if (config.disk_folder_path.empty()) {
    std::cout << "Enter the path to the disk folder: ";
    std::cin >> config.disk_folder_path;
  }

  std::ofstream out_config_file(config_file_path);
  out_config_file << config.iso_folder_path << '\n' << config.disk_folder_path << '\n';

  return config;
}

size_t write_data(void * ptr, size_t size, size_t nmemb, FILE * stream) {
  size_t written = fwrite(ptr, size, nmemb, stream);
  return written;
}

enum class OS {
  ubuntu,
  debian,
  arch
};

bool is_url_active(const Str & url) {
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

Vec<Str> debian_iso_urls = {
  "https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-12.4.0-amd64-netinst.iso",
  "https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-10.9.0-amd64-netinst.iso",
  "https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-10.9.0-amd64-netinst.iso",
};

Str check_iso_urls(const Vec<Str> &iso_urls) {
  for (const Str & url : iso_urls) {
    if (is_url_active(url)) {
      return url;
    } else {
      std::cout << "The ISO file cannot be downloaded from: " << url << '\n';
    }
  }
  return "";
}

Str fetch_iso_url(OS os) {
  switch(os) {
    case OS::ubuntu: 
      return "";
    case OS::debian: 
      return check_iso_urls(debian_iso_urls);
    case OS::arch: 
      return "";
    default:
      return "";
  }
}

void download_iso(const Str & url, const Str &output_file_path) {
  CURL* curl;
  FILE* fp;
  CURLcode res;

  curl = curl_easy_init();
  if(curl) {
    fp = fopen(output_file_path.c_str(), "wb");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    res = curl_easy_perform(curl);
    /* Check for errors */ 
    if(res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }

    /* always cleanup */ 
    curl_easy_cleanup(curl);
    fclose(fp);
  }
}

int download_iso_torrent(std::string magnet_link) {
  lt::session s;
  lt::add_torrent_params p;
  lt::error_code ec;
  lt::parse_magnet_uri(magnet_link, p, ec);
  if(ec) {
    std::cerr << "Error: " << ec.message() << std::endl;
    return 1;
  }

  p.save_path = "./";
  lt::torrent_handle h = s.add_torrent(p);

  while (!h.status().is_seeding) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Progress: " << h.status().progress * 100 << "%" << std::endl;
  }

  std::cout << "Download completed" << std::endl;

  return 0;
}

Vec<Str> qemu_args = {
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

void launchQemuVM(const Vec<Str> & args) {
  std::ostringstream cmd;
  cmd << "qemu-system-x86_64 ";

  for(const Str & arg : args) {
    cmd << arg << ' ';
  }

  std::system(cmd.str().c_str());
}

void create_qcow2_disk(const std::string& disk_path, int size_gb) {
  std::string command = "qemu-img create -f qcow2 " + disk_path + " " + std::to_string(size_gb) + "G";
  int result = std::system(command.c_str());
  if(result != 0) {
    std::cerr << "Failed to create disk image\n";
  }
}

void create_vm(const Str & name, OS os) {
  conf config = read_or_prompt_for_config("virt-cli.conf");
  Str iso_url = fetch_iso_url(os);
  if (iso_url.empty()) {
    std::cerr << "No valid ISO URL found. Aborting." << std::endl;
    return;
  }
  std::system(("mkdir -p " + config.iso_folder_path).c_str());
  std::system(("wget " + iso_url + " -O " + config.iso_folder_path + "/" + name + ".iso").c_str());

  int size_gb;
  std::cout << "Enter the size of the disk image in GB: ";
  std::cin >> size_gb;

  std::system(("mkdir -p " + config.disk_folder_path).c_str());
  create_qcow2_disk(config.disk_folder_path + "/" + name, size_gb);
}

void create_arch_vm(const Str & name, OS os) {
    conf config = read_or_prompt_for_config("virt-cli.conf");
    std::system(("mkdir -p " + config.iso_folder_path).c_str());
    int status = download_iso_torrent("magnet:?xt=urn:btih:1447bb03de993e1ee7e430526ff1fbac0daf7b44&dn=archlinux-2024.01.01-x86_64.iso");

    int size_gb;
    std::cout << "Enter the size of the disk image in GB: ";
    std::cin >> size_gb;

    std::system(("mkdir -p " + config.disk_folder_path).c_str());
    create_qcow2_disk(config.disk_folder_path + "/" + name, size_gb);
}

Str prompt_for_vm_name() {
  Str name;
  std::cout << "Enter the name of the VM: ";
  std::cin >> name;
  return name;
}

void start_vm(const Str & name) {
  conf config = read_or_prompt_for_config("virt-cli.conf");
  Str boot_order;
  while (true) {
    std::cout << "Enter [1] for disk_boot, Enter [2] for iso_boot: ";
    int choice;
    std::cin >> choice;
    if(choice == 1) {
      boot_order = "order=c";
      break;
    } 
    if(choice == 2) {
      boot_order = "order=d";
      break;
    } 
    std::cout << "Invalid choice. Please try again.\n";
  }

  Vec<Str> qemu_args = {
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

struct MenuOption {
  std::string name;
  std::function<void()> action;
};

void display_menu(const Vec<MenuOption>& options) {
  while(true) {
    std::cout << "\nMenu:\n";
    for(size_t i = 0; i < options.size(); ++i) {
      std::cout << i + 1 << ". " << options[i].name << '\n';
    }
    std::cout << "Enter the number of your choice, or 0 to go back: ";

    int choice;
    std::cin >> choice;

    if(choice == 0) {
      break;
    }
    if(choice > 0 && choice <= options.size()) {
      options[choice - 1].action();
      break;
    } 
    std::cout << "Invalid choice. Please try again.\n";
  }
}

Vec<MenuOption> new_vm = {
  {"debian", []() {
    create_vm(prompt_for_vm_name(), OS::debian);
  }},
  {"ubuntu", []() { 
    std::cout << "You chose sub-option 2.\n"; 
  }},
  {"arch_linux", []() {
    create_arch_vm(prompt_for_vm_name(), OS::arch);
  }}
};

Vec<MenuOption> vm_menu = {
  {"Start VM", []() {
    start_vm(prompt_for_vm_name());
  }},
  {"Stop VM", []() {
    std::cout << "You chose sub-option 2.\n";
  }},
  {"Delete VM", []() {
    std::cout << "You chose sub-option 3.\n";
  }},
  {"New VM", []() {
    display_menu(new_vm);
  }},
};

Vec<MenuOption> disk_menu = {
  {"Create new disk", []() {
    conf config = read_or_prompt_for_config("virt-cli.conf");
    Str disk_name;
    int size_gb;

    std::cout << "Enter the Name to the disk image: ";
    std::getline(std::cin, disk_name);

    std::cout << "Enter the size of the disk image in GB: ";
    std::cin >> size_gb;

    create_qcow2_disk(config.disk_folder_path + "/" + disk_name, size_gb);
  }},
  {"Delete disk", []() {
    std::cout << "You chose sub-option 2.\n";
  }},
};

Vec<MenuOption> main_menu = {
  {"check config", []() {
    conf config = read_or_prompt_for_config("virt-cli.conf");
    std::cout << "iso folder path: " << config.iso_folder_path << '\n';
    std::cout << "disk folder path: " << config.disk_folder_path << '\n';
  }},
  {"vm_menu",  []() {
    display_menu(vm_menu);
  }},
  {"disk_menu", []() {
    display_menu(disk_menu);
  }},
};

int main() {
  display_menu(main_menu);
  return 0;
}