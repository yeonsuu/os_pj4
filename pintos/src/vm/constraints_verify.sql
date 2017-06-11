

select * 
from Item 
where not exists ( select * from User where SellerID = User.UserID);

select *
from Bids
where not exists (select * from User where BidderID = User.UserID);

select *
from Bids
where not exists (select * from Item where Item.ItemID = ItemID);

select *
from Category
where not exists (select * from Item where ItemID = Item.ItemID);


