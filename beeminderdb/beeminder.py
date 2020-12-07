from pymongo import MongoClient
import pprint
import datetime
import json

class BeeMinder:

    dummy_data = {
        "temp" : 123,
        "humidity" : 321,
        "weight" : 100,
        "audio" : None
    }

    default_atlas_str = "mongodb+srv://base_station:eXSGDTaNpdQOhUKS@beeminder.tbykz.mongodb.net/BeeMinder"
    def __init__(self, atlas_str=default_atlas_str):
        self.client = MongoClient(atlas_str)
        self.db = self.client.BeeMinder
    
    def replace_owner(self, collection, current_owner, new_owner):
        db_filter = {"_owner" : current_owner}
        db_update = {"$set":{"_owner" : new_owner}}
        result = self.db[collection].update_many(db_filter, db_update)
        return (result.matched_count, result.modified_count)

    def add_report(self, hive_identifier, sensor_data):
        """
        Adds a ReportBoc to the database and updates the hive with the given hive_identifier,
        returns a String representation of the report_id
        If no hive exists then the report is not added to the database and False is returned.

        Effect: creates a ReportDoc, and adds the report_id to the HiveDoc given by hive_id 
        
        String, ReportDoc.sensor_data -> ReportDoc._id | False
        """
        hive = self.get_hive(hive_identifier)
        if hive is None:
            return False
        dt = datetime.datetime.now()
        report = {
            "_owner" : hive["_owner"],
            "time_recorded" : dt,
            "sensor_data" : sensor_data
        }
        report_id = False
        with self.client.start_session() as session:
            with session.start_transaction():
                ins_res = self.db.Reports.insert_one(report)
                report_id = ins_res.inserted_id
                self.db.Hives.update_one(
                    {"_id" : hive["_id"]},
                    {"$addToSet": {"reports": report_id}})
        return report_id
                
    def get_hive(self, hive_id):
        """
        fetches the hive document with the specified hive_id from the database. 
        If no document exists then False is retuned.

        String -> HiveDoc | False
        """
        result = self.db.Hives.find_one({"identifier": hive_id})
        if result == None:
            return False
        return result

    def get_data_from_file(self, path):
        """
        Reads from a json object from the file specified by path. and retuens the data object inside

        Path -> ReportDoc.sensor_data
        """
        sensor_data = None
        with open(path, "r") as file:
            sensor_data = json.load(file)
        return sensor_data

    def add_report_from_file(self, hive_identifier, path):
        """
        Reads the report data from the specified path and then adds the report
        to the hive doc specified with hive_identifier. On a sucessful add the 
        ReportDoc._id is returned. If the report could not be added False is returned

        String, Path -> ReportDoc._id | False
        """
        data = self.get_data_from_file(path)
        result = self.add_report(hive_identifier, data)
        return result

    def translate_report(self, report_id):
        """
        Increases the reports fields to include the flags array and 
        replaces the audio data with an fft array
        """
        pass
        # result = self.db.Reports.find_one