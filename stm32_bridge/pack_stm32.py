import os
import shutil

def main():
    # Paths
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = os.path.dirname(script_dir)
    
    inc_dest = os.path.join(script_dir, "Inc")
    src_dest = os.path.join(script_dir, "Src")
    
    # Create directories
    os.makedirs(inc_dest, exist_ok=True)
    os.makedirs(src_dest, exist_ok=True)
    
    # Headers to copy from include/
    headers = [
        "config.h",
        "protocol.h",
        "cc1101_regs.h",
        "cc1101_stm32.h",
        "usb_cdc_bridge.h",
        "tdma.h",
        "tdma_runtime.h",
        "ekf.h",
        "node_table.h",
        "radio_metrics.h"
    ]
    
    # Sources to copy from common/
    sources_common = [
        "protocol.c",
        "cc1101_regs.c",
        "radio_metrics.c",
        "tdma.c",
        "tdma_runtime.c",
        "node_table.c",
        "ekf.c"
    ]
    
    # Sources to copy from stm32_bridge/ (directly inside script_dir)
    sources_bridge = [
        "cc1101.c",
        "usb_cdc_bridge.c"
    ]
    
    print("Packing STM32 headers...")
    for h in headers:
        src_file = os.path.join(root_dir, "include", h)
        dst_file = os.path.join(inc_dest, h)
        if os.path.exists(src_file):
            shutil.copy2(src_file, dst_file)
            print(f"  Copied: {h} -> Inc/")
        else:
            print(f"  Warning: {src_file} not found!")
            
    print("Packing STM32 common sources...")
    for s in sources_common:
        src_file = os.path.join(root_dir, "common", s)
        dst_file = os.path.join(src_dest, s)
        if os.path.exists(src_file):
            shutil.copy2(src_file, dst_file)
            print(f"  Copied: {s} -> Src/")
        else:
            print(f"  Warning: {src_file} not found!")
            
    print("Packing STM32 bridge sources...")
    for s in sources_bridge:
        src_file = os.path.join(script_dir, s)
        dst_file = os.path.join(src_dest, s)
        if os.path.exists(src_file):
            shutil.copy2(src_file, dst_file)
            print(f"  Copied: {s} -> Src/")
        else:
            print(f"  Warning: {src_file} not found!")
            
    # Auto-synchronize to Keil project (link16-node) if it exists on the system
    keil_dir = r"C:\Users\devSh\Documents\link16\link16-node\Core"
    if os.path.exists(keil_dir):
        print("\nAuto-synchronizing to Keil project (link16-node)...")
        keil_inc = os.path.join(keil_dir, "Inc")
        keil_src = os.path.join(keil_dir, "Src")
        
        # Copy headers
        for h in headers:
            src_file = os.path.join(inc_dest, h)
            dst_file = os.path.join(keil_inc, h)
            if os.path.exists(src_file):
                shutil.copy2(src_file, dst_file)
                print(f"  Synced header: {h} -> Keil Core/Inc/")
                
        # Copy common sources
        for s in sources_common:
            src_file = os.path.join(src_dest, s)
            dst_file = os.path.join(keil_src, s)
            if os.path.exists(src_file):
                shutil.copy2(src_file, dst_file)
                print(f"  Synced source: {s} -> Keil Core/Src/")
                
        # Copy bridge sources
        for s in sources_bridge:
            src_file = os.path.join(src_dest, s)
            dst_file = os.path.join(keil_src, s)
            if os.path.exists(src_file):
                shutil.copy2(src_file, dst_file)
                print(f"  Synced source: {s} -> Keil Core/Src/")
                
        # Copy tdma_app.c (which is uniquely under stm32_bridge/Src/tdma_app.c)
        app_src = os.path.join(src_dest, "tdma_app.c")
        app_dst = os.path.join(keil_src, "tdma_app.c")
        if os.path.exists(app_src):
            shutil.copy2(app_src, app_dst)
            print(f"  Synced source: tdma_app.c -> Keil Core/Src/")
            
        print("Keil Project Synchronization Complete!")
            
    print("\nSTM32 Packaging Complete! You can now copy stm32_bridge/Inc and stm32_bridge/Src directly into your Keil/CubeMX project.")

if __name__ == "__main__":
    main()
