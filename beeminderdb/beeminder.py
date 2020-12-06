from pymongo import MongoClient
import pprint

class BeeMinder:
    old_user = "5fc541100dab7243052b2a23"
    joe_user = "5fc8516e6e63c036f9d9ad08"
    default_atlas_str = "mongodb+srv://base_station:eXSGDTaNpdQOhUKS@beeminder.tbykz.mongodb.net/BeeMinder"
    def __init__(self, atlas_str=default_atlas_str):
        self.client = MongoClient(atlas_str)
        self.db = self.client.BeeMinder
    
    def replace_owner(self, collection, current_owner, new_owner):
        db_filter = {"_owner" : current_owner}
        db_update = {"$set":{"_owner" : new_owner}}
        result = self.db[collection].update_many(db_filter, db_update)
        return (result.matched_count, result.modified_count)

    def add_report(self, hive_id, sensor_data):
        """
        Adds a ReportBoc to the database and updates the hive with the given hive_id,
        returns a String representation of the report_id
        If no hive exists then the report is not added to the database and False is returned.

        Effect: creates a ReportDoc, and adds the report_id to the HiveDoc given by hive_id 
        
        String, SensorData -> String | False
        """
        hive_filter = {"identifier": hive_id}
        hive_update = {"$addToSet":{"reports":""}}

    def get_hive(self, hive_id):
        """
        fetches the hive document with the specified hive_id from the database. 
        If no document exists then False is retuned.

        String -> HiveDoc | False
        """
        result = self.db.Hives.find_one({"identifier": hive_id})
        if result == None
            return False
        return result