from __future__ import print_function # Python 2/3 compatibility
import boto3
import json
import decimal
from boto3.dynamodb.conditions import Key, Attr
import settings
from botocore.exceptions import ClientError

class DynamoDB():

    DATABASE = 'dynamodb'
    REGION = settings.REGION_NAME
    TABLE = settings.TABLE_NAME

    ITEM = u'Items'

    json_data = ""

    def __init__(self, json_data):
        self.json_data = json.dumps(json_data)

    def get_database(self):
        try:
            dynamodb = boto3.resource(self.DATABASE, region_name=self.REGION)
            return dynamodb
        except ClientError as e:
            print("Unexpected error: {}".format(e))
            return

    def get_table(self):
        return self.get_database().Table(self.TABLE)

    def put_item_to_table(self, table, item):
        table.put_item(Item={
                            'name': item['name'],
                            'age': item['age'],
                            'distance': item['distance'],
                            'date': item['date'],
                            'gender': item['gender']
            }
        )

    def put_item_into_db(self):
        items = json.loads(self.json_data, parse_float = decimal.Decimal)
        self.put_item_to_table(self.get_table(), items)
        print("Done")

    def filter_values(self):
        datas = None
        for key, value in json.loads(self.json_data, parse_float = decimal.Decimal).items():
            if datas == None:
                datas = Key(key).eq(value)
            else:
                datas = datas & Key(key).eq(value)
        response = self.get_table().scan(FilterExpression=datas)
        for i in response[self.ITEM]:
            print(json.dumps(i, cls=DecimalEncoder))
        return response[self.ITEM]

    # Helper compare json
    def ordered(obj):
        result = obj
        if isinstance(obj, dict):
            result = sorted((k, ordered(v)) for k, v in obj.items())
        if isinstance(obj, list):
            result = sorted(ordered(x) for x in obj)
        return result


# Helper class to convert a DynamoDB item to JSON.
class DecimalEncoder(json.JSONEncoder):
    def default(self, item):
        obj = super(DecimalEncoder, self).default(item)
        if isinstance(item, decimal.Decimal):
            if item % 1 > 0:
                obj = float(item)
            else:
                obj = int(item)
        return obj
