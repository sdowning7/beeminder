#!/usr/bin/python3

import dbus, subprocess


from beeminder import BeeMinder
from advertisement import Advertisement
from service import Application, Service, Characteristic, Descriptor

GATT_CHRC_IFACE = "org.bluez.GattCharacteristic1"
NOTIFY_TIMEOUT = 5000
SMALL_SENSOR_DATA_LEN = 12
AUDIO_DATA_CHUNK_LEN = 496
END_DATA_LEN = 4


class BaseStationAdvertisement(Advertisement):
    def __init__(self, index):
        Advertisement.__init__(self, index, "peripheral")
        self.add_local_name("HIVE BASE")
        self.include_tx_power = True

class BaseStationService(Service):
    BASE_SVC_UUID =  "00ff" #"00000007-710e-4ab5-8d75-3e5b444bc3cf" #TODO something with this
    
    def __init__(self, index):
        
        Service.__init__(self, index, self.BASE_SVC_UUID, True)
        self.add_characteristic(BaseStationCharacteristic(self))

class BaseStationCharacteristic(Characteristic):
    BASE_CHAR_UUID = "ff01" #"00000077-710e-4ab5-8d75-3e5b444bc3cf" #TODO something with this

    def __init__(self, service):
        self.notifying = False
        self.raw_files = {}
        self.packet_count = 0
        Characteristic.__init__(self, self.BASE_CHAR_UUID, ["notify", "write"], service)
        

    def WriteValue(self, value, options):
        path = options['device'].split('/')
        name = path[-1]
        
        #print(len(value)) 
        if len(value) == SMALL_SENSOR_DATA_LEN:
            print("starting new sensor upload")
            print(self.packet_count)
            self.packet_count = 0
            # open a new file, erasing what is there
            try:
                self.raw_files[name] = open('Data/' + name + '.in', 'wb') 
            except FileNotFoundError:
                print('Failed to open file')
            
            #begin writing
            try:
                self.raw_files[name].write(bytes(value))
            except Exception as e:
                print(e)

        elif len(value) == AUDIO_DATA_CHUNK_LEN:
            self.packet_count += 1
            #write packets to a file
            try:
                self.raw_files[name].write(bytes(value))
            except Exception as e:
                print(e)
               
        elif len(value) == END_DATA_LEN:
            print('finished the sensor upload!')
            try:
                self.raw_files[name].close()
                print('file closed')
            except Exception as e:
                print(e)

            #run FFT to get output JSON
            self.runFFT(name)

            #send output JSON to database
            self.SendToDataBase(name)    
    

    def runFFT(self, name):
        in_file = open('Data/' + name + '.in', 'r')
        out_file = open('Data/' + name + '.json', 'w')

        subprocess.run('../beeminder_base_processing/hive_process', 
                stdin=in_file, stdout=out_file)
 
    def SendToDataBase(self, name):
        try:
            bm = BeeMinder()
            
            file_name = 'Data/'+name+'.json' 
            bm.add_report_from_file(name, file_name)
            print('data sent to database')
        except Exception as e:
            print(e)

    def StartNotify(self):
        pass

    def StopNotify(self):
        self.notifying = False


def main():
    app = Application()
    app.add_service(BaseStationService(0))
    app.register()

    adv = BaseStationAdvertisement(0)
    adv.register()

    try:
        app.run()
    except KeyboardInterrupt:
        app.quit()

if __name__ == "__main__":
    main()

