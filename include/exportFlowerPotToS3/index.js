const AWS = require('aws-sdk');
const s3 = new AWS.S3();
const S3_BUCKET = 'flowerpotbucket';
const S3_PREFIX = 'maceta/';

exports.handler = async (event) => {
  console.log("🧠 STREAM EVENT:", JSON.stringify(event, null, 2));

  for (const record of event.Records || []) {
    if (record.eventName !== 'INSERT' && record.eventName !== 'MODIFY') continue;

    const item = AWS.DynamoDB.Converter.unmarshall(record.dynamodb.NewImage);
    const timestamp = item.timestamp || Date.now();
    const key = `${S3_PREFIX}${item.thing_name}-${timestamp}.json`;

    const params = {
      Bucket: S3_BUCKET,
      Key: key,
      Body: JSON.stringify(item),
      ContentType: 'application/json'
    };

    try {
      await s3.putObject(params).promise();
      console.log(`✅ Exported to S3: ${key}`);
    } catch (err) {
      console.error(`❌ Error exporting ${key}`, err);
    }
  }
};
